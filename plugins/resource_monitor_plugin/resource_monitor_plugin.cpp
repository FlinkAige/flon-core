/**
   It was reported from a customer that when file system which
   "data/blocks" belongs to is running out of space, the producer
   continued to produce blocks and update state but the blocks log was
   "corrupted" in that it no longer contained all the irreversible blocks.
   It was also observed that when file system which "data/state"
   belons to is running out of space, funod will crash with SIGBUS as
   the state file is unable to acquire new pages.

   The solution is to have a dedicated plugin to monitor resource
   usages (file system space now, CPU, memory, and networking
   bandwidth in the future).
   The plugin uses a thread to periodically check space usage of file
   systems of directories being monitored. If space used
   is over a predefined threshold, a graceful shutdown is initiated.
**/

#include <eosio/resource_monitor_plugin/resource_monitor_plugin.hpp>
#include <eosio/resource_monitor_plugin/file_space_handler.hpp>
#include <eosio/resource_monitor_plugin/system_file_space_provider.hpp>

#include <eosio/chain/exceptions.hpp>

#include <fc/exception/exception.hpp>
#include <fc/log/logger_config.hpp> // set_os_thread_name

#include <thread>

#include <sys/stat.h>

using namespace eosio::resource_monitor;


namespace eosio {
   static auto _resource_monitor_plugin = application::register_plugin<resource_monitor_plugin>();

class resource_monitor_plugin_impl {
public:
   resource_monitor_plugin_impl()
      :space_handler(system_file_space_provider())
      {
      }

   void set_program_options(options_description&, options_description& cfg) {
      cfg.add_options()
         ( "resource-monitor-interval-seconds", bpo::value<uint32_t>()->default_value(def_interval_in_secs),
           "Time in seconds between two consecutive checks of resource usage. Should be between 1 and 300" )
         ( "resource-monitor-space-threshold", bpo::value<uint32_t>()->default_value(def_space_threshold),
           "Threshold in terms of percentage of used space vs total space. If used space is above (threshold - 5%), a warning is generated. "
           "Unless resource-monitor-not-shutdown-on-threshold-exceeded is enabled, a graceful shutdown is initiated if used space is above the threshold. "
           "The value should be between 6 and 99" )
         ( "resource-monitor-space-absolute-gb", bpo::value<uint64_t>(),
           "Absolute threshold in gibibytes of remaining space; applied to each monitored directory. "
           "If remaining space is less than value for any monitored directories then threshold is considered exceeded."
           "Overrides resource-monitor-space-threshold value.")
         ( "resource-monitor-not-shutdown-on-threshold-exceeded",
           "Used to indicate funod will not shutdown when threshold is exceeded." )
         ( "resource-monitor-warning-interval", bpo::value<uint32_t>()->default_value(def_monitor_warning_interval),
           "Number of resource monitor intervals between two consecutive warnings when the threshold is hit. Should be between 1 and 450" )
         ;
   }

   void plugin_initialize(const appbase::variables_map& options) {
      dlog("plugin_initialize");
      try{
         auto interval = options.at("resource-monitor-interval-seconds").as<uint32_t>();
         EOS_ASSERT(interval >= monitor_interval_min && interval <= monitor_interval_max, chain::plugin_config_exception,
            "\"resource-monitor-interval-seconds\" must be between ${monitor_interval_min} and ${monitor_interval_max}", ("monitor_interval_min", monitor_interval_min) ("monitor_interval_max", monitor_interval_max));
         space_handler.set_sleep_time(interval);
         ilog("Monitoring interval set to ${interval}", ("interval", interval));

         if (options.count("resource-monitor-space-absolute-gb")) {
            uint64_t v = options.at("resource-monitor-space-absolute-gb").as<uint64_t>();
            auto max = std::numeric_limits<uint64_t>::max()/1024/1024/1024;
            EOS_ASSERT(v > 0 && v < max, chain::plugin_config_exception,
                       "\"resource-monitor-space-absolute-gb\" must be greater than 0 GiB and less than max 64 bit value.");
            uint64_t w = v < 10 ? v+1 : std::min(max, (v + v/10)); // set reasonable absolute warning levels
            space_handler.set_absolute(v*1024*1024*1024, w*1024*1024*1024);
            ilog("Space usage absolute threshold set to ${v} GiB, warning set to ${w} GiB", ("v", v)("w",w));
         } else {
            auto threshold = options.at("resource-monitor-space-threshold").as<uint32_t>();
            EOS_ASSERT(threshold >= space_threshold_min  && threshold <= space_threshold_max, chain::plugin_config_exception,
               "\"resource-monitor-space-threshold\" must be between ${space_threshold_min} and ${space_threshold_max}",
               ("space_threshold_min", space_threshold_min) ("space_threshold_max", space_threshold_max));
            space_handler.set_threshold(threshold, threshold - space_threshold_warning_diff);
            ilog("Space usage threshold set to ${threshold}%", ("threshold", threshold));
         }

         if (options.count("resource-monitor-not-shutdown-on-threshold-exceeded")) {
            // If set, not shutdown
            space_handler.set_shutdown_on_exceeded(false);
            ilog("Shutdown flag when threshold exceeded set to false");
         } else {
            // Default will shut down
            space_handler.set_shutdown_on_exceeded(true);
            ilog("Shutdown flag when threshold exceeded set to true");
         }

         auto warning_interval = options.at("resource-monitor-warning-interval").as<uint32_t>();
         EOS_ASSERT(warning_interval >= warning_interval_min && warning_interval <= warning_interval_max, chain::plugin_config_exception,
            "\"resource-monitor-warning-interval\" must be between ${warning_interval_min} and ${warning_interval_max}", ("warning_interval_min", warning_interval_min) ("warning_interval_max", warning_interval_max));
         space_handler.set_warning_interval(warning_interval);
         ilog("Warning interval set to ${warning_interval}", ("warning_interval", warning_interval));
      } catch( const fc::exception& e) {
         elog( "resource monitor: ${e}", ("e",e.to_detail_string()) );
         throw;
      }
   }

   // Start main thread
   void plugin_startup() {
      space_handler.start(directories_registered);
   }

   // System is shutting down.
   void plugin_shutdown() {
      ilog("entered shutdown...");
      space_handler.stop();
      ilog("exiting shutdown");
   }

   void monitor_directory(const std::filesystem::path& path) {
      dlog("${path} registered to be monitored", ("path", path.string()));
      directories_registered.push_back(path);
   }

private:
   std::thread               monitor_thread;
   std::vector<std::filesystem::path>    directories_registered;

   static constexpr uint32_t def_interval_in_secs = 2;
   static constexpr uint32_t monitor_interval_min = 1;
   static constexpr uint32_t monitor_interval_max = 300;

   static constexpr uint32_t def_space_threshold = 90; // in percentage
   static constexpr uint32_t space_threshold_min = 6; // in percentage
   static constexpr uint32_t space_threshold_max = 99; // in percentage
   static constexpr uint32_t space_threshold_warning_diff = 5; // Warning issued when space used reached (threshold - space_threshold_warning_diff). space_threshold_warning_diff must be smaller than space_threshold_min

   static constexpr uint32_t def_monitor_warning_interval = 30; // After this number of monitor intervals, warning is output if the threshold is hit
   static constexpr uint32_t warning_interval_min = 1;
   static constexpr uint32_t warning_interval_max = 450; // e.g. if the monitor interval is 2 sec, the warning interval is at most 15 minutes

   using file_space_handler_t = file_space_handler<system_file_space_provider>;
   file_space_handler_t space_handler;
};

resource_monitor_plugin::resource_monitor_plugin():my(std::make_unique<resource_monitor_plugin_impl>()) {}

resource_monitor_plugin::~resource_monitor_plugin() {}

void resource_monitor_plugin::set_program_options(options_description& cli, options_description& cfg) {
   my->set_program_options(cli, cfg);
}

void resource_monitor_plugin::plugin_initialize(const variables_map& options) {
   my->plugin_initialize(options);
}

void resource_monitor_plugin::plugin_startup() {
   my->plugin_startup();
}

void resource_monitor_plugin::plugin_shutdown() {
   my->plugin_shutdown();
}

void resource_monitor_plugin::monitor_directory(const std::filesystem::path& path) {
   my->monitor_directory( path );
}

} // namespace
