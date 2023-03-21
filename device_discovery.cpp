/*
necessary headers such as <iostream>, <cstring>, <chrono>, <thread>, <sstream>, and <vector>.
Define some constants such as BROKER_ADDRESS, BROKER_PORT, DEVICE_CHECK_INTERVAL, NEW_DEVICE_TOPIC, DEVICE_TOPIC, and ALERT_TOPIC.
Define a function called "on_message" that handles incoming messages from the MQTT broker.
Define a function called "get_ip_address" that retrieves the IP address of the device.
Define a function called "get_mac_address" that retrieves the MAC address of the device.
Define a function called "run_network_monitoring" that performs the main network monitoring functionality.
In "run_network_monitoring", initialize the Mosquitto library and create a Mosquitto instance.
Set the username and password for the Mosquitto instance.
Connect to the MQTT broker.
Set the callback function for handling incoming messages.
Subscribe to the new device topic.
Loop through the list of devices on the network.
Execute the "arp" command and capture the output.
Build a vector of the MAC addresses from the output.
Sort the list of MAC addresses.
If the list of devices has changed, we update the device list and publish a message.
Create a JSON object with the device details.
Publish the device details to the device topic.
Publish an alert message for the new device.
Wait for a short period before checking for devices again.
Clean up and disconnect from the MQTT broker.
In "main", we run the network monitoring program by calling "run_network_monitoring".
*/

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cstdlib>

// External libraries for MQTT communication
#include <mosquitto.h>

using namespace std::chrono_literals;

// Define the broker details
const char* BROKER_ADDRESS = "localhost";
const int BROKER_PORT = 1883;

// Define the topics to use for publishing and subscribing
const char* DEVICE_TOPIC = "NETWORK/device";
const char* ALERT_TOPIC = "NETWORK/alert";
const char* NEW_DEVICE_TOPIC = "NETWORK/new_device";

// Mosquitto client instance
struct mosquitto* mosq = NULL;

// Current list of devices on the network
std::vector<std::string> deviceList;

// Callback function to handle messages received on subscribed topics
void on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* msg)
{
    // Extract the message payload
    std::string message(static_cast<const char*>(msg->payload), msg->payloadlen);

    std::cout << "Message received on topic: " << msg->topic << ". Message: " << message << std::endl;

    // If a new device connects to the network, send an alert message
    if (std::string(msg->topic) == NEW_DEVICE_TOPIC)
    {
        mosquitto_publish(mosq, NULL, ALERT_TOPIC, strlen("New device connected to the network"), "New device connected to the network", 0, false);
    }
}

// Get the MAC address of the device
std::string get_mac_address()
{
    // Execute the "ifconfig" command and capture the output
    std::ostringstream command;
    command << "/sbin/ifconfig | grep -o -E '([[:xdigit:]]{1,2}:){5}[[:xdigit:]]{1,2}' | head -n 1";
    FILE* stream = popen(command.str().c_str(), "r");
    if (!stream)
    {
        return "";
    }

    std::string result;
    char buffer[256];
    while (fgets(buffer, 256, stream) != NULL)
    {
        result += buffer;
    }
    pclose(stream);

    // Remove any newline characters from the output
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());

    return result;
}

// Get the IP address of the device
std::string get_ip_address()
{
    // Execute the "hostname -I" command and capture the output
    std::ostringstream command;
    command << "/bin/hostname -I | awk '{print $1}'";
    FILE* stream = popen(command.str().c_str(), "r");
    if (!stream)
    {
        return "";
    }

    std::string result;
    char buffer[256];
    while (fgets(buffer, 256, stream) != NULL)
    {
        result += buffer;
    }
    pclose(stream);

    // Remove any newline characters from the output
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());

    return result;
}

// Main function
int main(int argc, char* argv[])
{
    // Initialize the Mosquitto library
    mosquitto_lib_init();

    // Create a new Mosquitto client instance
    mosq = mosquitto_new("device_publisher", true, NULL);
    if (!mosq)
    {
        std::cerr << "Error: Unable to create Mosquitto client instance" << std::endl
        return 1;
}

// Connect to the MQTT broker
int ret = mosquitto_connect(mosq, BROKER_ADDRESS, BROKER_PORT, 60);
if (ret != MOSQ_ERR_SUCCESS)
{
    std::cerr << "Error: Unable to connect to MQTT broker" << std::endl;
    mosquitto_destroy(mosq);
    return 1;
}

// Set the callback function for handling incoming messages
mosquitto_message_callback_set(mosq, on_message);

// Subscribe to the new device topic
ret = mosquitto_subscribe(mosq, NULL, NEW_DEVICE_TOPIC, 0);
if (ret != MOSQ_ERR_SUCCESS)
{
    std::cerr << "Error: Unable to subscribe to topic " << NEW_DEVICE_TOPIC << std::endl;
    mosquitto_destroy(mosq);
    return 1;
}

// Loop through the list of devices on the network
while (true)
{
    // Execute the "arp" command and capture the output
    std::ostringstream command;
    command << "/usr/sbin/arp -a | awk '{print $2}'";
    FILE* stream = popen(command.str().c_str(), "r");
    if (!stream)
    {
        std::cerr << "Error: Unable to execute ARP command" << std::endl;
        break;
    }

    // Build a vector of the MAC addresses from the output
    std::vector<std::string> newDeviceList;
    char buffer[256];
    while (fgets(buffer, 256, stream) != NULL)
    {
        std::string macAddress = buffer;
        macAddress.erase(std::remove(macAddress.begin(), macAddress.end(), '\n'), macAddress.end());
        newDeviceList.push_back(macAddress);
    }
    pclose(stream);

    // Sort the list of MAC addresses
    std::sort(newDeviceList.begin(), newDeviceList.end());

    // If the list of devices has changed, update the device list and publish a message
    if (newDeviceList != deviceList)
    {
        // Update the device list
        deviceList = newDeviceList;

        // Create a JSON object with the device details
        std::ostringstream json;
        json << "{";
        json << "\"ip_address\":\"" << get_ip_address() << "\",";
        json << "\"mac_address\":\"" << get_mac_address() << "\",";
        json << "\"devices\":[";
        for (size_t i = 0; i < deviceList.size(); ++i)
        {
            json << "\"" << deviceList[i] << "\"";
            if (i < deviceList.size() - 1)
            {
                json << ",";
            }
        }
        json << "]}";

        // Publish the device details to the device topic
        ret = mosquitto_publish(mosq, NULL, DEVICE_TOPIC, json.str().length(), json.str().c_str(), 0, false);
        if (ret != MOSQ_ERR_SUCCESS)
        {
            std::cerr << "Error: Unable to publish device details to topic " << DEVICE_TOPIC << std::endl;
            break;
        }

        // Publish an alert message for the new device
        if (deviceList.size() > newDeviceList.size())
        {
            mosquitto_publish(mosq, NULL, ALERT_TOPIC, strlen("Device disconnected from the network"), "Device disconnected from the network", 0, false);
        }
        else if (deviceList.size() < newDeviceList.size())
        {
            mosquitto_publish(mosq, NULL, ALERT_TOPIC, strlen("New device connected to the network"), "New device connected to the network", 0, false);
        }
    }
    // Wait for a short period before checking for devices again
    std::this_thread::sleep_for(std::chrono::seconds(DEVICE_CHECK_INTERVAL));
}

// Clean up and disconnect from the MQTT broker
mosquitto_disconnect(mosq);
mosquitto_destroy(mosq);
mosquitto_lib_cleanup();
return 0;
}

int main()
{
// Run the network monitoring program
return run_network_monitoring();
}
