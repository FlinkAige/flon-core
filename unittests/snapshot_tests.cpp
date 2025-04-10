#include <sstream>

#include <eosio/chain/block_log.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/testing/tester.hpp>
#include "snapshot_suites.hpp"
#include <snapshots.hpp>
#include <snapshot_tester.hpp>

#include <boost/test/unit_test.hpp>

#include <test_contracts.hpp>
#include "test_wasts.hpp"

using namespace eosio;
using namespace testing;
using namespace chain;


namespace {
   void variant_diff_helper(const fc::variant& lhs, const fc::variant& rhs, std::function<void(const std::string&, const fc::variant&, const fc::variant&)>&& out){
      if (lhs.get_type() != rhs.get_type()) {
         out("", lhs, rhs);
      } else if (lhs.is_object() ) {
         const auto& l_obj = lhs.get_object();
         const auto& r_obj = rhs.get_object();
         static const std::string sep = ".";

         // test keys from LHS
         std::set<std::string_view> keys;
         for (const auto& entry: l_obj) {
            const auto& l_val = entry.value();
            const auto& r_iter = r_obj.find(entry.key());
            if (r_iter == r_obj.end()) {
               out(sep + entry.key(), l_val, fc::variant());
            } else {
               const auto& r_val = r_iter->value();
               variant_diff_helper(l_val, r_val, [&out, &entry](const std::string& path, const fc::variant& lhs, const fc::variant& rhs){
                  out(sep + entry.key() + path, lhs, rhs);
               });
            }

            keys.insert(entry.key());
         }

         // print keys in RHS that were not tested
         for (const auto& entry: r_obj) {
            if (keys.find(entry.key()) != keys.end()) {
               continue;
            }
            const auto& r_val = entry.value();
            out(sep + entry.key(), fc::variant(), r_val);
         }
      } else if (lhs.is_array()) {
         const auto& l_arr = lhs.get_array();
         const auto& r_arr = rhs.get_array();

         // diff common
         auto common_count = std::min(l_arr.size(), r_arr.size());
         for (size_t idx = 0; idx < common_count; idx++) {
            const auto& l_val = l_arr.at(idx);
            const auto& r_val = r_arr.at(idx);
            variant_diff_helper(l_val, r_val, [&](const std::string& path, const fc::variant& lhs, const fc::variant& rhs){
               out( std::string("[") + std::to_string(idx) + std::string("]") + path, lhs, rhs);
            });
         }

         // print lhs additions
         for (size_t idx = common_count; idx < lhs.size(); idx++) {
            const auto& l_val = l_arr.at(idx);
            out( std::string("[") + std::to_string(idx) + std::string("]"), l_val, fc::variant());
         }

         // print rhs additions
         for (size_t idx = common_count; idx < rhs.size(); idx++) {
            const auto& r_val = r_arr.at(idx);
            out( std::string("[") + std::to_string(idx) + std::string("]"), fc::variant(), r_val);
         }

      } else if (!(lhs == rhs)) {
         out("", lhs, rhs);
      }
   }

   void print_variant_diff(const fc::variant& lhs, const fc::variant& rhs) {
      variant_diff_helper(lhs, rhs, [](const std::string& path, const fc::variant& lhs, const fc::variant& rhs){
         std::cout << path << std::endl;
         if (!lhs.is_null()) {
            std::cout << " < " << fc::json::to_pretty_string(lhs) << std::endl;
         }

         if (!rhs.is_null()) {
            std::cout << " > " << fc::json::to_pretty_string(rhs) << std::endl;
         }
      });
   }

   template <typename SNAPSHOT_SUITE>
   void verify_integrity_hash(controller& lhs, controller& rhs) {
      const auto lhs_integrity_hash = lhs.calculate_integrity_hash();
      const auto rhs_integrity_hash = rhs.calculate_integrity_hash();
      if constexpr (std::is_same_v<SNAPSHOT_SUITE, variant_snapshot_suite>) {
         if(lhs_integrity_hash.str() != rhs_integrity_hash.str()) {
            auto lhs_latest_writer = SNAPSHOT_SUITE::get_writer();
            lhs.write_snapshot(lhs_latest_writer);
            auto lhs_latest = SNAPSHOT_SUITE::finalize(lhs_latest_writer);

            auto rhs_latest_writer = SNAPSHOT_SUITE::get_writer();
            rhs.write_snapshot(rhs_latest_writer);
            auto rhs_latest = SNAPSHOT_SUITE::finalize(rhs_latest_writer);

            print_variant_diff(lhs_latest, rhs_latest);
            // more than print the different, also save snapshots json gz files under path build/unittests/snapshots
            SNAPSHOT_SUITE::write_to_file("snapshot_debug_verify_integrity_hash_lhs", lhs_latest);
            SNAPSHOT_SUITE::write_to_file("snapshot_debug_verify_integrity_hash_rhs", rhs_latest);
         }
      }
      BOOST_REQUIRE_EQUAL(lhs_integrity_hash.str(), rhs_integrity_hash.str());
   }
}

// Split the tests into multiple parts which run approximately the same time
// so that they can finish within CICD time limits
BOOST_AUTO_TEST_SUITE(snapshot_part1_tests)

template<typename TESTER, typename SNAPSHOT_SUITE>
void exhaustive_snapshot_test()
{
   TESTER chain;

   // Create 2 accounts
   chain.create_accounts({"snapshot"_n, "snapshot1"_n});

   // Set code and increment the first account
   chain.produce_block();
   chain.set_code("snapshot"_n, test_contracts::snapshot_test_wasm());
   chain.set_abi("snapshot"_n, test_contracts::snapshot_test_abi());
   chain.produce_block();
   chain.push_action("snapshot"_n, "increment"_n, "snapshot"_n, mutable_variant_object()
         ( "value", 1 )
   );

   // Set code and increment the second account
   chain.produce_block();
   chain.set_code("snapshot1"_n, test_contracts::snapshot_test_wasm());
   chain.set_abi("snapshot1"_n, test_contracts::snapshot_test_abi());
   chain.produce_block();
   // increment the test contract
   chain.push_action("snapshot1"_n, "increment"_n, "snapshot1"_n, mutable_variant_object()
         ( "value", 1 )
   );

   chain.produce_block();

   chain.control->abort_block();

   static const int generation_count = 8;
   std::list<snapshotted_tester> sub_testers;

   for (int generation = 0; generation < generation_count; generation++) {
      // create a new snapshot child
      auto writer = SNAPSHOT_SUITE::get_writer();
      chain.control->write_snapshot(writer);
      auto snapshot = SNAPSHOT_SUITE::finalize(writer);

      // create a new child at this snapshot
      sub_testers.emplace_back(chain.get_config(), SNAPSHOT_SUITE::get_reader(snapshot), generation);

      // increment the test contract
      chain.push_action("snapshot"_n, "increment"_n, "snapshot"_n, mutable_variant_object()
         ( "value", 1 )
      );
      chain.push_action("snapshot1"_n, "increment"_n, "snapshot1"_n, mutable_variant_object()
         ( "value", 1 )
      );

      // produce block
      auto new_block = chain.produce_block();

      // undo the auto-pending from tester
      chain.control->abort_block();

      auto integrity_value = chain.control->calculate_integrity_hash();

      // push that block to all sub testers and validate the integrity of the database after it.
      for (auto& other: sub_testers) {
         other.push_block(new_block);
         BOOST_REQUIRE_EQUAL(integrity_value.str(), other.control->calculate_integrity_hash().str());
      }
   }
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_exhaustive_snapshot, SNAPSHOT_SUITE, snapshot_suites)
{
   exhaustive_snapshot_test<legacy_tester, SNAPSHOT_SUITE>();
   exhaustive_snapshot_test<savanna_tester, SNAPSHOT_SUITE>();
}

template<typename TESTER, typename SNAPSHOT_SUITE>
void replay_over_snapshot_test()
{
   TESTER chain;
   const std::filesystem::path parent_path = chain.get_config().blocks_dir.parent_path();

   chain.create_account("snapshot"_n);
   chain.produce_block();
   chain.set_code("snapshot"_n, test_contracts::snapshot_test_wasm());
   chain.set_abi("snapshot"_n, test_contracts::snapshot_test_abi());
   chain.produce_block();
   chain.control->abort_block();

   static const int pre_snapshot_block_count = 12;
   static const int post_snapshot_block_count = 12;

   for (int itr = 0; itr < pre_snapshot_block_count; itr++) {
      // increment the contract
      chain.push_action("snapshot"_n, "increment"_n, "snapshot"_n, mutable_variant_object()
         ( "value", 1 )
      );

      // produce block
      chain.produce_block();
   }

   chain.control->abort_block();

   // create a new snapshot child
   auto writer = SNAPSHOT_SUITE::get_writer();
   chain.control->write_snapshot(writer);
   auto snapshot = SNAPSHOT_SUITE::finalize(writer);

   // create a new child at this snapshot
   int ordinal = 1;
   snapshotted_tester snap_chain(chain.get_config(), SNAPSHOT_SUITE::get_reader(snapshot), ordinal++);
   verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *snap_chain.control);

   // push more blocks to build up a block log
   for (int itr = 0; itr < post_snapshot_block_count; itr++) {
      // increment the contract
      chain.push_action("snapshot"_n, "increment"_n, "snapshot"_n, mutable_variant_object()
         ( "value", 1 )
      );

      // produce & push block
      snap_chain.push_block(chain.produce_block());
   }

   // verify the hash at the end
   chain.control->abort_block();
   verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *snap_chain.control);

   // replay the block log from the snapshot child, from the snapshot
   using config_file_handling = snapshotted_tester::config_file_handling;
   snapshotted_tester replay_chain(snap_chain.get_config(), SNAPSHOT_SUITE::get_reader(snapshot), ordinal++, config_file_handling::copy_config_files);
   const auto replay_head = replay_chain.head().block_num();
   auto snap_head = snap_chain.head().block_num();
   BOOST_REQUIRE_EQUAL(replay_head, snap_chain.last_irreversible_block_num());
   for (auto block_num = replay_head + 1; block_num <= snap_head; ++block_num) {
      auto block = snap_chain.fetch_block_by_number(block_num);
      replay_chain.push_block(block);
   }
   verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *replay_chain.control);

   auto block = chain.produce_block();
   chain.control->abort_block();
   snap_chain.push_block(block);
   replay_chain.push_block(block);
   verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *snap_chain.control);
   verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *replay_chain.control);

   snapshotted_tester replay2_chain(snap_chain.get_config(), SNAPSHOT_SUITE::get_reader(snapshot), ordinal++, config_file_handling::copy_config_files);
   const auto replay2_head = replay2_chain.head().block_num();
   snap_head = snap_chain.head().block_num();
   BOOST_REQUIRE_EQUAL(replay2_head, snap_chain.last_irreversible_block_num());
   for (auto block_num = replay2_head + 1; block_num <= snap_head; ++block_num) {
      auto block = snap_chain.fetch_block_by_number(block_num);
      replay2_chain.push_block(block);
   }
   verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *replay2_chain.control);

   // verifies that chain's block_log has a genesis_state (and blocks starting at 1)
   controller::config copied_config = copy_config_and_files(chain.get_config(), ordinal++);
   auto genesis = chain::block_log::extract_genesis_state(chain.get_config().blocks_dir);
   BOOST_REQUIRE(genesis);
   tester from_block_log_chain(copied_config, *genesis);
   const auto from_block_log_head = from_block_log_chain.head().block_num();
   BOOST_REQUIRE_EQUAL(from_block_log_head, snap_chain.last_irreversible_block_num());
   for (auto block_num = from_block_log_head + 1; block_num <= snap_head; ++block_num) {
      auto block = snap_chain.fetch_block_by_number(block_num);
      from_block_log_chain.push_block(block);
   }
   verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *from_block_log_chain.control);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_replay_over_snapshot, SNAPSHOT_SUITE, snapshot_suites)
{
   replay_over_snapshot_test<legacy_tester, SNAPSHOT_SUITE>();
   replay_over_snapshot_test<savanna_tester, SNAPSHOT_SUITE>();
}

template<typename TESTER, typename SNAPSHOT_SUITE>
void chain_id_in_snapshot_test()
{
   TESTER chain;
   const std::filesystem::path parent_path = chain.get_config().blocks_dir.parent_path();

   chain.create_account("snapshot"_n);
   chain.produce_block();
   chain.set_code("snapshot"_n, test_contracts::snapshot_test_wasm());
   chain.set_abi("snapshot"_n, test_contracts::snapshot_test_abi());
   chain.produce_block();
   chain.control->abort_block();

   // create a new snapshot child
   auto writer = SNAPSHOT_SUITE::get_writer();
   chain.control->write_snapshot(writer);
   auto snapshot = SNAPSHOT_SUITE::finalize(writer);

   snapshotted_tester snap_chain(chain.get_config(), SNAPSHOT_SUITE::get_reader(snapshot), 0);
   BOOST_REQUIRE_EQUAL(chain.get_chain_id(), snap_chain.get_chain_id());
   verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *snap_chain.control);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_chain_id_in_snapshot, SNAPSHOT_SUITE, snapshot_suites)
{
   chain_id_in_snapshot_test<legacy_tester, SNAPSHOT_SUITE>();
   chain_id_in_snapshot_test<savanna_tester, SNAPSHOT_SUITE>();
}

BOOST_AUTO_TEST_SUITE_END()
BOOST_AUTO_TEST_SUITE(snapshot_part2_tests)

static auto get_extra_args() {
   bool save_snapshot = false;
   bool generate_log = false;

   auto argc = boost::unit_test::framework::master_test_suite().argc;
   auto argv = boost::unit_test::framework::master_test_suite().argv;
   std::for_each(argv, argv + argc, [&](const std::string &a){
      if (a == "--save-snapshot") {
         save_snapshot = true;
      } else if (a == "--generate-snapshot-log") {
         generate_log = true;
      }
   });

   return std::make_tuple(save_snapshot, generate_log);
}

template<typename TESTER, typename SNAPSHOT_SUITE>
void compatible_versions_test()
{
   const uint32_t legacy_default_max_inline_action_size = 4 * 1024;
   bool save_snapshot = false;
   bool generate_log = false;
   std::tie(save_snapshot, generate_log) = get_extra_args();
   const auto source_log_dir = std::filesystem::path(snapshot_file<snapshot::binary>::base_path);

   if (generate_log) {
      ///< Begin deterministic code to generate blockchain for comparison

      TESTER chain(setup_policy::none, db_read_mode::HEAD, {legacy_default_max_inline_action_size});
      chain.create_account("snapshot"_n);
      chain.produce_block();
      chain.set_code("snapshot"_n, test_contracts::snapshot_test_wasm());
      chain.set_abi("snapshot"_n, test_contracts::snapshot_test_abi());
      chain.produce_block();
      chain.control->abort_block();

      // continue until all the above blocks are in the blocks.log
      auto head_block_num = chain.head().block_num();
      while (chain.last_irreversible_block_num() < head_block_num) {
         chain.produce_block();
      }

      auto source = chain.get_config().blocks_dir / "blocks.log";
      std::filesystem::copy_file(source, source_log_dir / "blocks.log", std::filesystem::copy_options::overwrite_existing);
      auto source_i = chain.get_config().blocks_dir / "blocks.index";
      std::filesystem::copy_file(source_i, source_log_dir / "blocks.index", std::filesystem::copy_options::overwrite_existing);
      chain.close();
   }
   fc::temp_directory temp_dir;
   auto config = tester::default_config(temp_dir, legacy_default_max_inline_action_size).first;
   auto genesis = eosio::chain::block_log::extract_genesis_state(source_log_dir);
   std::filesystem::create_directories(config.blocks_dir);
   std::filesystem::copy(source_log_dir / "blocks.log", config.blocks_dir / "blocks.log");
   std::filesystem::copy(source_log_dir / "blocks.index", config.blocks_dir / "blocks.index");
   TESTER base_chain(config, *genesis);

   std::string current_version = "v8";

   int ordinal = 0;
   for(std::string version : {"v2", "v3", "v4", "v5", "v6", "v8"}) // v7 version not supported in fullon 1.0.1 and above
   {
      if(save_snapshot && version == current_version) continue;
      static_assert(chain_snapshot_header::minimum_compatible_version <= 2, "version 2 unit test is no longer needed.  Please clean up data files");
      auto old_snapshot = SNAPSHOT_SUITE::load_from_file("snap_" + version);
      BOOST_TEST_CHECKPOINT("loading snapshot: " << version);
      snapshotted_tester old_snapshot_tester(base_chain.get_config(), SNAPSHOT_SUITE::get_reader(old_snapshot), ordinal++);
      verify_integrity_hash<SNAPSHOT_SUITE>(*base_chain.control, *old_snapshot_tester.control);

      // create a latest snapshot
      auto latest_writer = SNAPSHOT_SUITE::get_writer();
      old_snapshot_tester.control->write_snapshot(latest_writer);
      auto latest = SNAPSHOT_SUITE::finalize(latest_writer);

      // load the latest snapshot
      snapshotted_tester latest_tester(base_chain.get_config(), SNAPSHOT_SUITE::get_reader(latest), ordinal++);
      verify_integrity_hash<SNAPSHOT_SUITE>(*base_chain.control, *latest_tester.control);
   }
   // This isn't quite fully automated.  The snapshots still need to be gzipped and moved to
   // the correct place in the source tree.
   // -------------------------------------------------------------------------------------------------------------
   // Process for supporting a new snapshot version in this test:
   // ----------------------------------------------------------
   // 1. update `current_version` and the list of versions in `for` loop
   // 2. run: `unittests/unit_test -t "snapshot_tests/test_com*" -- --save-snapshot` to generate new snapshot files
   // 3. copy the newly generated files (see `ls -lrth ./unittests/snapshots/snap_*` to `fullon/unittests/snapshots`
   //    for example `cp ./unittests/snapshots/snap_v8.* ../unittests/snapshots`
   // 4. edit `unittests/snapshots/CMakeLists.txt` and add the `configure_file` commands for the 3 new files.
   //    now the test should pass.
   // 5. add the 3 new snapshot files in git.
   // -------------------------------------------------------------------------------------------------------------
   // Only want to save one snapshot, use Savanna as that is the latest
   // And skip the threaded_snapshot_suite: its saving-to-file ability isn't implemented and, besides, its snapshot
   //  creation half just uses the ostream snapshot writer again (using writer_t = ostream_snapshot_writer)
   if constexpr (std::is_same_v<TESTER, savanna_tester> && !std::is_same_v<SNAPSHOT_SUITE, threaded_snapshot_suite>) {
      if (save_snapshot)
      {
         // create a latest snapshot
         auto latest_writer = SNAPSHOT_SUITE::get_writer();
         base_chain.control->write_snapshot(latest_writer);
         auto latest = SNAPSHOT_SUITE::finalize(latest_writer);

         SNAPSHOT_SUITE::write_to_file("snap_" + current_version, latest);
      }
   }
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_compatible_versions, SNAPSHOT_SUITE, snapshot_suites)
{
   compatible_versions_test<legacy_tester, SNAPSHOT_SUITE>();
   compatible_versions_test<savanna_tester, SNAPSHOT_SUITE>();
}

/*
When WTMSIG changes were introduced in 1.8.x, the snapshot had to be able
to store more than a single producer key.
This test intends to make sure that a snapshot from before that change could
be correctly loaded into a new version to facilitate upgrading from 1.8.x
to v2.0.x without a replay.

The original test simulated a snapshot from 1.8.x with an inflight schedule change, loaded it on the newer version and reconstructed the chain via
push_transaction. This is too fragile.

The fix is to save block.log and its corresponding snapshot with infight
schedule changes, load the snapshot and replay the block.log on the new
version, and verify their integrity.
*/
template<typename TESTER, typename SNAPSHOT_SUITE>
void pending_schedule_snapshot_test()
{
   static_assert(chain_snapshot_header::minimum_compatible_version <= 2, "version 2 unit test is no longer needed.  Please clean up data files");

   // consruct a chain by replaying the saved blocks.log
   std::string source_log_dir_str = snapshot_file<snapshot::binary>::base_path;
   source_log_dir_str += "prod_sched";
   const auto source_log_dir = std::filesystem::path(source_log_dir_str.c_str());
   const uint32_t legacy_default_max_inline_action_size = 4 * 1024;
   fc::temp_directory temp_dir;
   auto config = tester::default_config(temp_dir, legacy_default_max_inline_action_size).first;
   auto genesis = eosio::chain::block_log::extract_genesis_state(source_log_dir);
   std::filesystem::create_directories(config.blocks_dir);
   std::filesystem::copy(source_log_dir / "blocks.log", config.blocks_dir / "blocks.log");
   std::filesystem::copy(source_log_dir / "blocks.index", config.blocks_dir / "blocks.index");
   TESTER blockslog_chain(config, *genesis);

   // consruct a chain by loading the saved snapshot
   auto ordinal = 0;
   auto old_snapshot = SNAPSHOT_SUITE::load_from_file("snap_v2_prod_sched");
   snapshotted_tester snapshot_chain(blockslog_chain.get_config(), SNAPSHOT_SUITE::get_reader(old_snapshot), ordinal++);

   // make sure blockslog_chain and snapshot_chain agree to each other
   verify_integrity_hash<SNAPSHOT_SUITE>(*blockslog_chain.control, *snapshot_chain.control);

   // extra round of testing
   // create a latest snapshot
   auto latest_writer = SNAPSHOT_SUITE::get_writer();
   snapshot_chain.control->write_snapshot(latest_writer);
   auto latest = SNAPSHOT_SUITE::finalize(latest_writer);

   // construct a chain from the latest snapshot
   snapshotted_tester latest_chain(blockslog_chain.get_config(), SNAPSHOT_SUITE::get_reader(latest), ordinal++);

   // make sure both chains agree
   verify_integrity_hash<SNAPSHOT_SUITE>(*blockslog_chain.control, *latest_chain.control);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_pending_schedule_snapshot, SNAPSHOT_SUITE, snapshot_suites)
{
   pending_schedule_snapshot_test<legacy_tester, SNAPSHOT_SUITE>();
   pending_schedule_snapshot_test<savanna_tester, SNAPSHOT_SUITE>();
}

template<typename TESTER, typename SNAPSHOT_SUITE>
void restart_with_existing_state_and_truncated_block_log_test()
{
   TESTER chain;
   const std::filesystem::path parent_path = chain.get_config().blocks_dir.parent_path();

   chain.create_account("snapshot"_n);
   chain.produce_block();
   chain.set_code("snapshot"_n, test_contracts::snapshot_test_wasm());
   chain.set_abi("snapshot"_n, test_contracts::snapshot_test_abi());
   chain.produce_block();
   chain.control->abort_block();

   static const int pre_snapshot_block_count = 12;

   for (int itr = 0; itr < pre_snapshot_block_count; itr++) {
      // increment the contract
      chain.push_action("snapshot"_n, "increment"_n, "snapshot"_n, mutable_variant_object()
                        ( "value", 1 )
                        );

      // produce block
      chain.produce_block();
   }

   chain.control->abort_block();
   {
      // create a new snapshot child
      auto writer = SNAPSHOT_SUITE::get_writer();
      chain.control->write_snapshot(writer);
      auto snapshot = SNAPSHOT_SUITE::finalize(writer);

      // create a new child at this snapshot
      int ordinal = 1;

      snapshotted_tester snap_chain(chain.get_config(), SNAPSHOT_SUITE::get_reader(snapshot), ordinal++);
      verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *snap_chain.control);
      auto block = chain.produce_block();
      chain.control->abort_block();
      snap_chain.push_block(block);
      verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *snap_chain.control);

      snap_chain.close();
      auto cfg = snap_chain.get_config();
      // restart chain with truncated block log and existing state, but no genesis state (chain_id)
      snap_chain.open();
      verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *snap_chain.control);

      block = chain.produce_block();
      chain.control->abort_block();
      snap_chain.push_block(block);
      verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *snap_chain.control);
   }
   // test with empty block log
   {
      // create a new snapshot child
      auto writer = SNAPSHOT_SUITE::get_writer();
      chain.control->write_snapshot(writer);
      auto snapshot = SNAPSHOT_SUITE::finalize(writer);

      // create a new child at this snapshot
      int ordinal = 2;
      auto chain_cfg = chain.get_config();
      chain_cfg.blog = eosio::chain::empty_blocklog_config{}; // use empty block log
      snapshotted_tester snap_chain(chain_cfg, SNAPSHOT_SUITE::get_reader(snapshot), ordinal++);
      verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *snap_chain.control);
      auto block = chain.produce_block();
      chain.control->abort_block();
      snap_chain.push_block(block);
      verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *snap_chain.control);

      snap_chain.close();
      auto cfg = snap_chain.get_config();
      // restart chain with truncated block log and existing state, but no genesis state (chain_id)
      snap_chain.open();
      verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *snap_chain.control);

      block = chain.produce_block();
      chain.control->abort_block();
      snap_chain.push_block(block);
      verify_integrity_hash<SNAPSHOT_SUITE>(*chain.control, *snap_chain.control);
   }

}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_restart_with_existing_state_and_truncated_block_log, SNAPSHOT_SUITE, snapshot_suites)
{
   restart_with_existing_state_and_truncated_block_log_test<legacy_tester, SNAPSHOT_SUITE>();
   restart_with_existing_state_and_truncated_block_log_test<savanna_tester, SNAPSHOT_SUITE>();
}

BOOST_AUTO_TEST_CASE_TEMPLATE( json_snapshot_validity_test, TESTER, testers )
{
   auto ordinal = 0;
   TESTER chain;

   // prep the chain
   chain.create_account("snapshot"_n);
   chain.produce_block();
   chain.set_code("snapshot"_n, test_contracts::snapshot_test_wasm());
   chain.set_abi("snapshot"_n, test_contracts::snapshot_test_abi());
   chain.produce_block();
   chain.control->abort_block();

   auto pid_string = std::to_string(getpid());
   auto bin_file = pid_string + "BinSnapshot";
   auto json_file = pid_string + "JsonSnapshot";
   auto bin_from_json_file = pid_string + "BinFromJsonSnapshot";

   // create bin snapshot
   auto writer_bin = buffered_snapshot_suite::get_writer();
   chain.control->write_snapshot(writer_bin);
   auto snapshot_bin = buffered_snapshot_suite::finalize(writer_bin);
   buffered_snapshot_suite::write_to_file(bin_file, snapshot_bin);

   // create json snapshot
   auto writer_json = json_snapshot_suite::get_writer();
   chain.control->write_snapshot(writer_json);
   auto snapshot_json = json_snapshot_suite::finalize(writer_json);
   json_snapshot_suite::write_to_file(json_file, snapshot_json);

   // load bin snapshot
   auto snapshot_bin_read = buffered_snapshot_suite::load_from_file(bin_file);
   auto reader_bin = buffered_snapshot_suite::get_reader(snapshot_bin_read);
   snapshotted_tester tester_bin(chain.get_config(), reader_bin, ordinal++);

   // load json snapshot
   auto snapshot_json_read = json_snapshot_suite::load_from_file(json_file);
   auto reader_json = json_snapshot_suite::get_reader(snapshot_json_read);
   snapshotted_tester tester_json(chain.get_config(), reader_json, ordinal++);

   // create bin snapshot from loaded json snapshot
   auto writer_bin_from_json = buffered_snapshot_suite::get_writer();
   tester_json.control->write_snapshot(writer_bin_from_json);
   auto snapshot_bin_from_json = buffered_snapshot_suite::finalize(writer_bin_from_json);
   buffered_snapshot_suite::write_to_file(bin_from_json_file, snapshot_bin_from_json);

   // load new bin snapshot
   auto snapshot_bin_from_json_read = buffered_snapshot_suite::load_from_file(bin_from_json_file);
   auto reader_bin_from_json = buffered_snapshot_suite::get_reader(snapshot_bin_from_json_read);
   snapshotted_tester tester_bin_from_json(chain.get_config(), reader_bin_from_json, ordinal++);

   // ensure all snapshots are equal
   verify_integrity_hash<buffered_snapshot_suite>(*tester_bin_from_json.control, *tester_bin.control);
   verify_integrity_hash<buffered_snapshot_suite>(*tester_bin_from_json.control, *tester_json.control);
   verify_integrity_hash<buffered_snapshot_suite>(*tester_json.control, *tester_bin.control);

   auto bin_snap_path = std::filesystem::path(snapshot_file<snapshot::binary>::base_path) / bin_file;
   auto bin_from_json_snap_path = std::filesystem::path(snapshot_file<snapshot::binary>::base_path) / bin_from_json_file;
   auto json_snap_path = std::filesystem::path(snapshot_file<snapshot::json>::base_path) / json_file;
   remove(bin_snap_path);
   remove(bin_from_json_snap_path);
   remove(json_snap_path);
}

template<typename TESTER, typename SNAPSHOT_SUITE>
void jumbo_row_test()
{
   fc::temp_directory tempdir;
   auto config = tester::default_config(tempdir);
   config.first.state_size = 64*1024*1024;
   TESTER chain(config.first, config.second);
   chain.execute_setup_policy(setup_policy::full);

   chain.create_accounts({"jumbo"_n});
   chain.set_code("jumbo"_n, set_jumbo_row_wast);
   chain.produce_block();

   signed_transaction trx;
   action act;
   act.account = "jumbo"_n;
   act.name = "jumbo"_n;
   act.authorization = vector<permission_level>{{"jumbo"_n,config::active_name}};
   trx.actions.push_back(act);

   chain.set_transaction_headers(trx);
   trx.sign(tester::get_private_key("jumbo"_n, "active"), chain.get_chain_id());
   chain.push_transaction(trx);
   chain.produce_block();

   chain.control->abort_block();

   auto writer = SNAPSHOT_SUITE::get_writer();
   chain.control->write_snapshot(writer);
   auto snapshot = SNAPSHOT_SUITE::finalize(writer);

   snapshotted_tester sst(chain.get_config(), SNAPSHOT_SUITE::get_reader(snapshot), 0);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(jumbo_row, SNAPSHOT_SUITE, snapshot_suites)
{
   jumbo_row_test<legacy_tester, SNAPSHOT_SUITE>();
   jumbo_row_test<savanna_tester, SNAPSHOT_SUITE>();
}

template<typename TESTER, typename SNAPSHOT_SUITE>
void removed_table_snapshot_test() {
   typename SNAPSHOT_SUITE::snapshot_t snap;
   controller::config cfg;
   {
      TESTER chain;

      chain.create_account("snapshot"_n);
      chain.set_code("snapshot"_n, test_contracts::snapshot_test_wasm());
      chain.set_abi("snapshot"_n, test_contracts::snapshot_test_abi());
      chain.produce_block();

      chain.push_action("snapshot"_n, "add"_n, "snapshot"_n, mutable_variant_object()("scope", "apple"_n) ("id", 1u)("payload",sha256::hash("1")));
      chain.push_action("snapshot"_n, "add"_n, "snapshot"_n, mutable_variant_object()("scope", "apple"_n) ("id", 2u)("payload",sha256::hash("2")));
      chain.push_action("snapshot"_n, "add"_n, "snapshot"_n, mutable_variant_object()("scope", "banana"_n)("id", 3u)("payload",sha256::hash("3")));
      chain.push_action("snapshot"_n, "add"_n, "snapshot"_n, mutable_variant_object()("scope", "banana"_n)("id", 4u)("payload",sha256::hash("4")));
      chain.push_action("snapshot"_n, "add"_n, "snapshot"_n, mutable_variant_object()("scope", "carrot"_n)("id", 5u)("payload",sha256::hash("5")));
      chain.push_action("snapshot"_n, "add"_n, "snapshot"_n, mutable_variant_object()("scope", "carrot"_n)("id", 6u)("payload",sha256::hash("6")));

      chain.produce_block();

      chain.push_action("snapshot"_n, "remove"_n, "snapshot"_n, mutable_variant_object()("scope", "banana"_n)("id", 3u));
      chain.push_action("snapshot"_n, "remove"_n, "snapshot"_n, mutable_variant_object()("scope", "banana"_n)("id", 4u));

      chain.produce_block();

      chain.control->abort_block();
      auto writer = SNAPSHOT_SUITE::get_writer();
      chain.control->write_snapshot(writer);
      snap = SNAPSHOT_SUITE::finalize(writer);
      cfg = chain.get_config();
   }
   {
      snapshotted_tester chain2(cfg, SNAPSHOT_SUITE::get_reader(snap), 0);

      chain2.push_action("snapshot"_n, "verify"_n, "snapshot"_n, mutable_variant_object()("scope", "carrot"_n)("id", 5u)("payload",sha256::hash("5")));
      chain2.push_action("snapshot"_n, "verify"_n, "snapshot"_n, mutable_variant_object()("scope", "carrot"_n)("id", 6u)("payload",sha256::hash("6")));
   }
}

BOOST_AUTO_TEST_CASE_TEMPLATE(removed_table, SNAPSHOT_SUITE, snapshot_suites) {
   removed_table_snapshot_test<legacy_tester, SNAPSHOT_SUITE>();
   removed_table_snapshot_test<savanna_tester, SNAPSHOT_SUITE>();
}

BOOST_AUTO_TEST_SUITE_END()
