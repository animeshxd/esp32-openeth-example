#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_http_server.h"

static const char *TAG = "eth_example";
static httpd_handle_t server = NULL;
static esp_netif_ip_info_t current_ip_info = {0};
static uint8_t current_mac[6] = {0};

// HTML page template
static const char* html_page = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"    <title>ESP32 OPENETH Network Info</title>"
"    <style>"
"        body { font-family: Arial, sans-serif; margin: 40px; background-color: #f5f5f5; }"
"        .container { background-color: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
"        h1 { color: #333; text-align: center; }"
"        .info-section { margin: 20px 0; padding: 15px; background-color: #f8f9fa; border-radius: 5px; }"
"        .info-label { font-weight: bold; color: #555; }"
"        .info-value { color: #007bff; margin-left: 10px; }"
"        .refresh-btn { background-color: #007bff; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; margin-top: 20px; }"
"        .refresh-btn:hover { background-color: #0056b3; }"
"    </style>"
"    <script>"
"        function refreshPage() { location.reload(); }"
"        setInterval(refreshPage, 5000);"
"    </script>"
"</head>"
"<body>"
"    <div class='container'>"
"        <h1>ESP32 OPENETH Network Information</h1>"
"        <div class='info-section'>"
"            <div><span class='info-label'>IP Address:</span><span class='info-value' id='ip'>%s</span></div>"
"            <div><span class='info-label'>Netmask:</span><span class='info-value' id='netmask'>%s</span></div>"
"            <div><span class='info-label'>Gateway:</span><span class='info-value' id='gateway'>%s</span></div>"
"            <div><span class='info-label'>MAC Address:</span><span class='info-value' id='mac'>%02x:%02x:%02x:%02x:%02x:%02x</span></div>"
"        </div>"
"        <button class='refresh-btn' onclick='refreshPage()'>Refresh Now</button>"
"        <p><small>Page auto-refreshes every 5 seconds</small></p>"
"    </div>"
"</body>"
"</html>";

// HTTP GET handler for root path
static esp_err_t root_get_handler(httpd_req_t *req)
{
    char ip_str[16], netmask_str[16], gateway_str[16];
    char response[2048];
    
    // Convert IP addresses to strings
    sprintf(ip_str, IPSTR, IP2STR(&current_ip_info.ip));
    sprintf(netmask_str, IPSTR, IP2STR(&current_ip_info.netmask));
    sprintf(gateway_str, IPSTR, IP2STR(&current_ip_info.gw));
    
    // Format the HTML response
    snprintf(response, sizeof(response), html_page, 
             ip_str, netmask_str, gateway_str,
             current_mac[0], current_mac[1], current_mac[2], 
             current_mac[3], current_mac[4], current_mac[5]);
    
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

// HTTP GET handler for JSON API
static esp_err_t api_get_handler(httpd_req_t *req)
{
    char json_response[512];
    
    snprintf(json_response, sizeof(json_response),
        "{"
        "\"ip\":\"" IPSTR "\","
        "\"netmask\":\"" IPSTR "\","
        "\"gateway\":\"" IPSTR "\","
        "\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\""
        "}",
        IP2STR(&current_ip_info.ip),
        IP2STR(&current_ip_info.netmask), 
        IP2STR(&current_ip_info.gw),
        current_mac[0], current_mac[1], current_mac[2], 
        current_mac[3], current_mac[4], current_mac[5]);
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
}

// URI handlers
static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t api = {
    .uri       = "/api/info",
    .method    = HTTP_GET,
    .handler   = api_get_handler,
    .user_ctx  = NULL
};

// Start HTTP server
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP Server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Register URI handlers
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &api);
        return server;
    }

    ESP_LOGI(TAG, "Error starting HTTP server!");
    return NULL;
}

// Stop HTTP server
static esp_err_t stop_webserver(httpd_handle_t server)
{
    return httpd_stop(server);
}

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        memcpy(current_mac, mac_addr, 6);  // Store MAC for web server
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        // Stop HTTP server when ethernet is disconnected
        if (server) {
            stop_webserver(server);
            server = NULL;
            ESP_LOGI(TAG, "HTTP Server stopped due to network disconnection");
        }
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    // Store IP info for web server
    memcpy(&current_ip_info, ip_info, sizeof(esp_netif_ip_info_t));

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");

    // Start HTTP server when we have an IP
    if (server == NULL) {
        server = start_webserver();
        if (server) {
            ESP_LOGI(TAG, "HTTP Server started. Access web interface at: http://" IPSTR, IP2STR(&ip_info->ip));
        }
    }
}

void app_main(void)
{
    // Initialize TCP/IP network interface (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create default ESP netif for Ethernet
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);

    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    // Create MAC and PHY configs for OPENETH
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.autonego_timeout_ms = 100;
    
    // Create MAC instance for OPENETH
    esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_config);
    
    // Create PHY instance for OPENETH (using DP83848 PHY)
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);
    
    // Create Ethernet driver handle
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));
    
    // Attach Ethernet driver to TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    
    // Start Ethernet driver state machine
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    
    ESP_LOGI(TAG, "Ethernet initialization complete, waiting for IP address...");
    ESP_LOGI(TAG, "HTTP server will start automatically when IP is acquired");
}
