# Realm C++ Rest Example

Goal of this project is to create a Realm C++ app taking REST calls with minimal code for simplicity.

realm-cpp Installation does not seem to work completely, we need to use add_subdirectory with relative paths (this makes the clean docker build pretty slow).

Using a fixed realm-cpp git version (see dockerfile) from 2023-02-15, change this to use a newer version.

## Build

Add dependencies for realm and the app (see dockerfile).

Build

```
mkdir build
cd build
cmake -GNinja ..
cmake --build .
```

## Docker

* Create a file ".env"
* Add one line with your app id: `APPID=app-id`
* run `docker-compose build`
* run `docker-compose up`
* Note: the image is not optimized, it gets pretty large

Results:

1. `sync: App: log_in_with_credentials failed: 404 message: cannot find app using Client App ID` means incorrect app id
1. `sync error: error` - don't know what this is, but the app-id is probably correct
1. `Starting to listen` - if this is reached it's up and running

## Usage

Send an object to the app

```
curl -X PUT http://localhost:9000/restdemo \
   -H "Content-Type: application/json" \
   -d '{"machine_id": 1}'
```