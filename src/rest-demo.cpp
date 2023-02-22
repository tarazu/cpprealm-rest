#include <iostream>
#include <cpprealm/sdk.hpp>
#include <cpprest/json.h>
#include <cpprest/http_listener.h>

typedef int64_t identifier_t;
typedef int64_t load_t;
typedef std::vector<double> position_t;
typedef std::chrono::time_point<std::chrono::system_clock> timestamp_t;

class InvocationQueue {
public:
    void push(std::function<void()>&&);
    void invoke_all();
    bool empty();

private:
    std::mutex m_mutex;
    std::vector<std::function<void()>> m_functions;
};

void InvocationQueue::push(std::function<void()>&& fn)
{
    std::lock_guard lock(m_mutex);
    m_functions.push_back(std::move(fn));
}

void InvocationQueue::invoke_all()
{
    std::vector<std::function<void()>> functions;
    {
        std::lock_guard lock(m_mutex);
        functions.swap(m_functions);
    }
    for (auto&& fn : functions) {
        fn();
    }
}

bool InvocationQueue::empty()
{
    std::lock_guard lock(m_mutex);
    bool empty = m_functions.empty();
    return empty;
}

struct IoTObject : realm::object<IoTObject>
{
    // db internal
    realm::persisted<realm::object_id> _id{realm::object_id::generate()};
    realm::persisted<identifier_t> owner_id;   // Id of the site, for multitenancy

    // user defined
    realm::persisted<identifier_t> machine_id;   // Id of the current machine_id, e.g. 1.
    realm::persisted<std::optional<identifier_t>> cycle_id;     // Id of the current production cycle, e.g. 1.
    realm::persisted<std::optional<timestamp_t>> cycle_start;   // The starting time of the current production cycle. This information will be in epoch time format.
    realm::persisted<std::optional<timestamp_t>> cycle_end;     // The ending time of the current production cycle. This information will be in epoch time format.
    realm::persisted<std::optional<load_t>> pay_load;           // The transported load in kg for the current production cycle, e.g. 17000 .
    realm::persisted<std::optional<std::string>> material_type; // The material of the load, e.g. sand.
    realm::persisted<position_t> dumping_spot;   // The longitude, latitude and altitude of the dumping spot.

    static constexpr auto schema = realm::schema(
            "IoTObject",
            realm::property<&IoTObject::_id, true>("_id"),
            realm::property<&IoTObject::owner_id>("owner_id"),

            realm::property<&IoTObject::machine_id>("machine_id"),
            realm::property<&IoTObject::cycle_id>("cycle_id"),
            realm::property<&IoTObject::cycle_start>("cycle_start"),
            realm::property<&IoTObject::cycle_end>("cycle_end"),
            realm::property<&IoTObject::pay_load>("pay_load"),
            realm::property<&IoTObject::material_type>("material_type"),
            realm::property<&IoTObject::dumping_spot>("dumping_spot"));
};

using namespace web;
using namespace web::http;
using namespace web::http::experimental::listener;

IoTObject model_from_json(web::json::object obj)
{
    auto data = IoTObject {
            .owner_id = 1 // needed for multitenancy, hardcoded for now
    };

    // this can maybe be done using the realm schema
    // also skipping type error handling for now
    web::json::object::const_iterator it;
    if ((it = obj.find("machine_id")) != obj.end())
        data.machine_id = it->second.as_number().is_int64();
    if ((it = obj.find("cycle_id")) != obj.end())
        data.cycle_id = it->second.as_number().is_int64();
    if ((it = obj.find("cycle_start")) != obj.end())
        data.cycle_start = timestamp_t(std::chrono::seconds(it->second.as_number().is_int64()));
    if ((it = obj.find("cycle_end")) != obj.end())
        data.cycle_end = timestamp_t(std::chrono::seconds(it->second.as_number().is_int64()));
    if ((it = obj.find("pay_load")) != obj.end())
        data.pay_load = load_t(it->second.as_number().is_int64());
    if ((it = obj.find("material_type")) != obj.end())
        data.material_type = it->second.as_string().c_str();
    if ((it = obj.find("dumping_spot")) != obj.end())
        if (it->second.is_array())
            for (auto &e : it->second.as_array())
                data.dumping_spot.push_back(e.as_double());

    return data;
}

int main(int argc, char const *argv[])
{
    std::string app_id = "insert-key";
    if (argc > 1)
    {
        std::cout << "Using user defined app id" << std::endl;
        app_id = std::string(argv[1]);
    }
    else
        std::cout << "Using default app id" << std::endl;

    // Create a new Device Sync App and turn development mode on under sync settings.
    auto app = realm::App(app_id);
    // auto user = app.login(realm::App::credentials::api_key(api_key)).get_future().get();
    auto user = app.login(realm::App::credentials::anonymous()).get_future().get();
    auto config = user.flexible_sync_configuration();
    http_listener listener(uri("http://0.0.0.0:9000/restdemo"));
    auto moveable_config_copy = config;
    {
        // Set up the Realm sync subscriptions initially.
        auto realm = realm::open<IoTObject>(std::move(moveable_config_copy));
        realm.subscriptions().update([](realm::mutable_sync_subscription_set& subs) {
            if (!subs.find("all_objects")) {
                subs.add<IoTObject>("all_objects");
            }
        }).get_future().get();
    }

    InvocationQueue queue;
    // queue.push(std::move(fn));

    listener.support(methods::PUT, [config, &queue](http_request request)
    {
        auto answer = json::value::object();
        request
                .extract_json()
                .then([&answer, config, &queue](pplx::task<json::value> task)
                      {
                          try
                          {
                              auto const & jvalue = task.get();
                              if (!jvalue.is_null()) {
                                  queue.push([&]()
                                  {
                                      auto moveable_config_copy = config;
                                      auto synced_realm = realm::open<IoTObject>(std::move(moveable_config_copy));
                                      if (jvalue.is_array())
                                      {
                                          for (auto const &e : jvalue.as_array())
                                          {
                                              auto data = model_from_json(e.as_object());
                                              // write to realm
                                              synced_realm.write([&synced_realm, &data]() {
                                                  synced_realm.add(data);
                                              });
                                          }
                                      } else {
                                          auto data = model_from_json(jvalue.as_object());
                                          // write to realm
                                          synced_realm.write([&synced_realm, &data]() {
                                              synced_realm.add(data);
                                          });
                                      }
                                  });
                                  while(!queue.empty())
                                    std::this_thread::sleep_for(std::chrono::seconds(1));
                              }
                          }
                          catch (http_exception const & e)
                          {
                              std::cout << e.what() << "\n";
                          }
                      }).wait();
        request.reply(status_codes::OK, answer); });
    try
    {
        listener
                .open()
                .then([&listener]()
                      { std::cout << "Starting to listen\n"
                                  << std::endl; })
                .wait();

        while(true)
        {
            queue.invoke_all();
            while(queue.empty())
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    catch (std::exception const &e)
    {
        std::cout << e.what() << std::endl;
    }


    return 0;
}
