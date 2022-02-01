
#pragma once

#include <fstream>

#include "util/logger.hpp"
#include "util/params.hpp"
#include "interface/json_interface.hpp"
#include "interface/api/api_connector.hpp"
#include "util/sys/fileutils.hpp"
#include "util/sys/background_worker.hpp"
#include "util/shuffle.hpp"

class JobStreamer {

private:
    const Parameters& _params;
    APIConnector& _api;
    int _internal_rank;
    nlohmann::json _json_template;
    bool _valid = false;

    int _job_counter = 1;
    int _job_description_index = 0;
    std::vector<std::string> _job_descriptions;

    BackgroundWorker _bg_worker;
    std::atomic_int _num_active_jobs = 0;
    Mutex _submit_mutex;
    ConditionVariable _submit_cond_var;

    BackgroundWorker _bg_deleter;
    std::atomic_int _num_jsons_to_delete = 0;
    Mutex _delete_mutex;
    std::vector<nlohmann::json> _jsons_to_delete;

public:
    JobStreamer(const Parameters& params, APIConnector& api, int internalRank) : 
            _params(params), _api(api), _internal_rank(internalRank) {
        
        std::string jsonTemplateFile = _params.jobTemplate();
        // Check if a client-specific template file is present
        std::string clientSpecificFile = jsonTemplateFile + "." + std::to_string(_internal_rank);
        if (FileUtils::isRegularFile(clientSpecificFile)) {
            jsonTemplateFile = clientSpecificFile;
        } else
        // Is template file itself valid?
        if (!FileUtils::isRegularFile(jsonTemplateFile)) {
            LOG(V1_WARN, "Job template file %s does not exist\n", jsonTemplateFile.c_str());        
            return;
        }

        // Attempt to parse JSON from file
        try {
            std::ifstream i(jsonTemplateFile);
            i >> _json_template;
            _valid = true;
        } catch (const nlohmann::detail::parse_error& e) {
            LOG(V1_WARN, "[WARN] Parse error on job template file %s: %s\n", 
                jsonTemplateFile.c_str(), e.what());
        }

        // Parse job description files
        if (_params.jobDescriptionTemplate.isSet()) {
            std::ifstream i(_params.jobDescriptionTemplate());
            std::string line;
            while (std::getline(i, line)) {
                if (!line.empty()) _job_descriptions.push_back(line);
            }
        }
        // Shuffle job description files if desired
        if (!_job_descriptions.empty() && _params.shuffleJobDescriptions()) {
            auto rng = std::mt19937(_params.seed() + _internal_rank); 
            auto dist = std::uniform_real_distribution<float>(0, 1); 
            auto rngFunc = [&]() {return dist(rng);};
            shuffle(_job_descriptions.data(), _job_descriptions.size(), rngFunc);
        }

        // Run concurrent submission of jobs
        _bg_worker.run([&]() {
            std::string baseJobName = _json_template["name"];

            while (_bg_worker.continueRunning()) {

                // Wait until there is work
                _submit_cond_var.wait(_submit_mutex, [&]() {
                    return !_bg_worker.continueRunning() || 
                        _num_active_jobs < _params.activeJobsPerClient();
                });
                if (!_bg_worker.continueRunning()) break;

                // Submit new jobs
                while (_num_active_jobs < _params.activeJobsPerClient()) {

                    // Prepare JSON
                    auto jsonCopy = _json_template;
                    jsonCopy["name"] = baseJobName + "-" + std::to_string(_job_counter++);
                    if (!_job_descriptions.empty()) {
                        assert(_job_description_index < _job_descriptions.size());
                        jsonCopy["files"] = std::vector<std::string>(1, _job_descriptions[_job_description_index]);
                        _job_description_index = (_job_description_index+1) % _job_descriptions.size();
                    }

                    _num_active_jobs++;
                    _api.submit(jsonCopy, [&](nlohmann::json& result) {
                        // Result for the job arrived.
                        {
                            // Garbage collect JSON
                            auto lock = _delete_mutex.getLock();
                            _jsons_to_delete.emplace_back(std::move(result));
                        }
                        _num_active_jobs--;
                        _num_jsons_to_delete++;
                        _submit_cond_var.notify();
                    });
                }
            }
        });

        _bg_deleter.run([&]() {
            while (_bg_deleter.continueRunning()) {

                _submit_cond_var.wait(_delete_mutex, [&]() {
                    return !_bg_deleter.continueRunning() ||
                        _num_jsons_to_delete > 0;
                });
                if (!_bg_deleter.continueRunning()) break;

                // Delete a garbage JSON
                while (_num_jsons_to_delete > 0) {
                    nlohmann::json jsonToDelete;
                    {
                        auto lock = _delete_mutex.getLock();
                        jsonToDelete = std::move(_jsons_to_delete.back());
                        _jsons_to_delete.pop_back();
                    }
                    _num_jsons_to_delete--;
                    // expensive JSON destructor is called here (out of mutex)
                }
            }
        });
    }

    ~JobStreamer() {
        _bg_worker.stopWithoutWaiting();
        _bg_deleter.stopWithoutWaiting();
        _submit_cond_var.notify();
        _bg_worker.stop();
        _bg_deleter.stop();
    }
};
