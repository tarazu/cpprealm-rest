#include <cpprealm/sdk.hpp>
#include <cpprest/json.h>
#include <cpprest/http_listener.h>

/* Definition of time, if using chrono it seems to play well with realm/mongo, int64 is an alternative */
namespace realm
{
    typedef std::chrono::time_point<std::chrono::system_clock> timestamp_t;
}
typedef realm::timestamp_t timestamp_t;
// typedef int64_t timestamp_t;

/* User defined types, not all desired types are supported in alpha */
typedef int64_t identifier_t;
typedef int64_t load_t;
typedef std::vector<double> position_t;

struct Model : realm::object<Model>
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
        realm::property<&Model::_id, true>("_id"),
        realm::property<&Model::owner_id>("owner_id"),

        realm::property<&Model::machine_id>("machine_id"),
        realm::property<&Model::cycle_id>("cycle_id"),
        realm::property<&Model::cycle_start>("cycle_start"),
        realm::property<&Model::cycle_end>("cycle_end"),
        realm::property<&Model::pay_load>("pay_load"),
        realm::property<&Model::material_type>("material_type"),
        realm::property<&Model::dumping_spot>("dumping_spot"));
};

using namespace std;
using namespace web;
using namespace web::http;
using namespace web::http::experimental::listener;

// example of how to create and write in c++
void add_object(realm::db<Model> &synced_realm)
{
    auto data = Model{
        .machine_id = 1};
    auto epoch = chrono::system_clock::now().time_since_epoch().count();
    data.cycle_start = timestamp_t(chrono::seconds(epoch));
    data.dumping_spot = position_t{0.0, 0.0, 0.0};

    synced_realm.write([&synced_realm, &data]()
                       { synced_realm.add(data); });
}

Model model_from_json(json::object obj)
{
    auto data = Model{
        .owner_id = 1 // needed for multitenancy, hardcoded for now
    };

    // this can maybe be done using the realm schema
    // also skipping type error handling for now
    json::object::const_iterator it;
    if ((it = obj.find("machine_id")) != obj.end())
        data.machine_id = it->second.as_number().is_int64();
    if ((it = obj.find("cycle_id")) != obj.end())
        data.cycle_id = it->second.as_number().is_int64();
    if ((it = obj.find("cycle_start")) != obj.end())
        data.cycle_start = timestamp_t(chrono::seconds(it->second.as_number().is_int64()));
    if ((it = obj.find("cycle_end")) != obj.end())
        data.cycle_end = timestamp_t(chrono::seconds(it->second.as_number().is_int64()));
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

    auto flx_sync_config = user.flexible_sync_configuration();
    auto tsr = realm::async_open<Model>(flx_sync_config).get_future().get();
    auto synced_realm = tsr.resolve();

    http_listener listener(uri("http://*:9000/restdemo"));

    listener.support(methods::PUT, [&synced_realm](http_request request)
                     {
        auto answer = json::value::object();

        request
        .extract_json()
        .then([&answer, &synced_realm](pplx::task<json::value> task)
              {
         try
         {
            auto const & jvalue = task.get();

            if (!jvalue.is_null())
            {
                if (jvalue.is_array())
                {
                    for (auto const &e : jvalue.as_array())
                    {
                        auto data = model_from_json(e.as_object());
                        // write to realm
                        synced_realm.write([&synced_realm, &data]()
                            { synced_realm.add(data); });
                    }
                }
                else{
                    auto data = model_from_json(jvalue.as_object());
                    // write to realm
                    synced_realm.write([&synced_realm, &data]()
                        { synced_realm.add(data); });
                }
            }
         }
         catch (http_exception const & e)
         {
            wcout << e.what() << endl;
         } })
        .wait();
        request.reply(status_codes::OK, answer); });

    try
    {
        listener
            .open()
            .then([&listener]()
                  { std::cout << "Starting to listen\n"
                              << std::endl; })
            .wait();

        while (true)
            ;
    }
    catch (exception const &e)
    {
        std::cout << e.what() << std::endl;
    }

    return 0;
}
