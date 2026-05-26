#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <vector>
#include "log.h"

struct ServiceEndpoint {
    std::string ip;
    short port;
    int weight = 1;

    std::string addr() const { return ip + ":" + std::to_string(port); }

    bool operator==(const ServiceEndpoint& other) const {
        return ip == other.ip && port == other.port;
    }
};

class ServiceRegistry {
public:
    void Register(const std::string& service_name, const ServiceEndpoint& endpoint)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& eps = services_[service_name];
        auto it = std::find(eps.begin(), eps.end(), endpoint);
        if (it == eps.end()) {
            eps.push_back(endpoint);
            LOG_INFO("[Registry] Registered %s -> %s", service_name.c_str(), endpoint.addr().c_str());
        }
    }

    void Deregister(const std::string& service_name, const ServiceEndpoint& endpoint)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = services_.find(service_name);
        if (it != services_.end()) {
            auto& eps = it->second;
            eps.erase(std::remove(eps.begin(), eps.end(), endpoint), eps.end());
            LOG_INFO("[Registry] Deregistered %s -> %s", service_name.c_str(), endpoint.addr().c_str());
        }
    }

    std::vector<ServiceEndpoint> Discover(const std::string& service_name)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = services_.find(service_name);
        if (it != services_.end()) {
            return it->second;
        }
        return {};
    }

    size_t ServiceCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return services_.size();
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::vector<ServiceEndpoint>> services_;
};

enum class LoadBalanceStrategy {
    RoundRobin,
    Random,
};

class LoadBalancer {
public:
    LoadBalancer(ServiceRegistry* registry,
                 LoadBalanceStrategy strategy = LoadBalanceStrategy::RoundRobin)
        : registry_(registry)
        , strategy_(strategy)
        , rng_(std::random_device{}())
    {}

    std::optional<ServiceEndpoint> GetEndpoint(const std::string& service,
                                                const std::string& route_key = "")
    {
        (void)route_key; // reserved for consistent-hash routing

        auto endpoints = registry_->Discover(service);
        if (endpoints.empty()) {
            LOG_ERROR("[LoadBalancer] No endpoints for service: %s", service.c_str());
            return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        if (strategy_ == LoadBalanceStrategy::RoundRobin) {
            size_t& idx = rr_index_[service];
            size_t selected = idx % endpoints.size();
            idx++;
            LOG_DEBUG("[LoadBalancer] RoundRobin: %s -> %s (index=%zu/%zu)",
                      service.c_str(), endpoints[selected].addr().c_str(), selected, endpoints.size());
            return endpoints[selected];
        }
        else // Random
        {
            std::uniform_int_distribution<size_t> dist(0, endpoints.size() - 1);
            size_t selected = dist(rng_);
            LOG_DEBUG("[LoadBalancer] Random: %s -> %s (index=%zu/%zu)",
                      service.c_str(), endpoints[selected].addr().c_str(), selected, endpoints.size());
            return endpoints[selected];
        }
    }

    void SetStrategy(LoadBalanceStrategy strategy)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        strategy_ = strategy;
        LOG_INFO("[LoadBalancer] Strategy changed to %s",
                 (strategy == LoadBalanceStrategy::RoundRobin ? "RoundRobin" : "Random"));
    }

private:
    ServiceRegistry* registry_;
    LoadBalanceStrategy strategy_;
    std::mutex mutex_;
    std::map<std::string, size_t> rr_index_;
    std::mt19937 rng_;
};
