#include "home_storage_engine.h"
#include <sisl/fds/utils.hpp>
#include <boost/uuid/uuid_io.hpp>

#define SM_STORE_LOG(level, msg, ...)                                                                                  \
    LOG##level##MOD_FMT(home_replication, ([&](fmt::memory_buffer& buf, const char* msgcb, auto&&... args) -> bool {   \
                            fmt::vformat_to(fmt::appender{buf}, fmt::string_view{"[{}:{}] "},                          \
                                            fmt::make_format_args(file_name(__FILE__), __LINE__));                     \
                            fmt::vformat_to(fmt::appender{buf}, fmt::string_view{"[{}={}] "},                          \
                                            fmt::make_format_args("rs", boost::uuids::to_string(m_sb_in_mem.uuid)));   \
                            fmt::vformat_to(fmt::appender{buf}, fmt::string_view{msgcb},                               \
                                            fmt::make_format_args(std::forward< decltype(args) >(args)...));           \
                            return true;                                                                               \
                        }),                                                                                            \
                        msg, ##__VA_ARGS__);

SISL_LOGGING_DECL(home_replication)

namespace home_replication {
static constexpr store_lsn_t to_store_lsn(repl_lsn_t raft_lsn) { return raft_lsn - 1; }
static constexpr repl_lsn_t to_repl_lsn(store_lsn_t store_lsn) { return store_lsn + 1; }

///////////////////////////// HomeStateMachineStore Section ////////////////////////////
HomeStateMachineStore::HomeStateMachineStore(uuid_t rs_uuid) : m_sb{"replica_set"} {
    LOGDEBUGMOD(home_replication, "Creating new instance of replica state machine store for uuid={}", rs_uuid);

    // Create a superblk for the replica set.
    m_sb.create(sizeof(home_rs_superblk));
    m_sb->uuid = rs_uuid;

    // Create logstore to store the free pba records
    m_free_pba_store =
        homestore::logstore_service().create_new_log_store(homestore::LogStoreService::CTRL_LOG_FAMILY_IDX, true);
    if (!m_free_pba_store) { throw std::runtime_error("Failed to create log store"); }
    m_sb->free_pba_store_id = m_free_pba_store->get_store_id();
    m_sb.write();
    m_sb_in_mem = *m_sb;
    SM_STORE_LOG(DEBUG, "New free pba record logstore={} created", m_sb->free_pba_store_id);
}

HomeStateMachineStore::HomeStateMachineStore(const homestore::superblk< home_rs_superblk >& rs_sb) :
        m_sb{"replica_set"} {
    LOGDEBUGMOD(home_replication, "Opening existing replica state machine store for uuid={}", rs_sb->uuid);
    m_sb = rs_sb;
    m_sb_in_mem = *m_sb;
    SM_STORE_LOG(DEBUG, "Opening free pba record logstore={}", m_sb->free_pba_store_id);
    homestore::logstore_service().open_log_store(homestore::LogStoreService::CTRL_LOG_FAMILY_IDX,
                                                 m_sb->free_pba_store_id, true,
                                                 bind_this(HomeStateMachineStore::on_store_created, 1));
}

void HomeStateMachineStore::on_store_created(std::shared_ptr< homestore::HomeLogStore > free_pba_store) {
    assert(m_sb->free_pba_store_id == free_pba_store->get_store_id());
    m_free_pba_store = free_pba_store;
    // m_free_pba_store->register_log_found_cb(
    //     [this](int64_t lsn, homestore::log_buffer buf, [[maybe_unused]] void* ctx) { m_entry_found_cb(lsn, buf); });
    SM_STORE_LOG(DEBUG, "Successfully opened free pba record logstore={}", m_sb->free_pba_store_id);
}

void HomeStateMachineStore::destroy() {
    SM_STORE_LOG(DEBUG, "Free pba record logstore={} is being physically removed", m_sb->free_pba_store_id);
    homestore::logstore_service().remove_log_store(homestore::LogStoreService::CTRL_LOG_FAMILY_IDX,
                                                   m_sb->free_pba_store_id);
    m_free_pba_store.reset();
}

pba_list_t HomeStateMachineStore::alloc_pbas(uint32_t) {
    // TODO: Implementation pending
    return pba_list_t{};
}

void HomeStateMachineStore::async_write(const sisl::sg_list&, const pba_list_t&, const io_completion_cb_t&) {
    // TODO: Implementation pending
}
void HomeStateMachineStore::async_read(pba_t, sisl::sg_list&, uint32_t, const io_completion_cb_t&) {
    // TODO: Implementation pending
}
void HomeStateMachineStore::free_pba(pba_t) {
    // TODO: Implementation pending
}

void HomeStateMachineStore::commit_lsn(repl_lsn_t lsn) {
    folly::SharedMutexWritePriority::ReadHolder holder(m_sb_lock);
    m_sb_in_mem.commit_lsn = lsn;
}

repl_lsn_t HomeStateMachineStore::get_last_commit_lsn() const {
    folly::SharedMutexWritePriority::ReadHolder holder(m_sb_lock);
    return m_sb_in_mem.commit_lsn;
}

void HomeStateMachineStore::add_free_pba_record(repl_lsn_t lsn, const pba_list_t& pbas) {
    // Serialize it as
    // # num pbas (N)       4 bytes
    // +---
    // | PBA                8 bytes
    // +--- repeat N
    uint32_t size_needed = sizeof(uint32_t) + (pbas.size() * sizeof(pba_t));
    sisl::io_blob b{size_needed, 0 /* unaligned */};
    *(r_cast< uint32_t* >(b.bytes)) = uint32_cast(pbas.size());

    pba_t* raw_ptr = r_cast< pba_t* >(b.bytes + sizeof(uint32_t));
    for (const auto pba : pbas) {
        *raw_ptr = pba;
        ++raw_ptr;
    }
    m_last_write_lsn.store(lsn);
    m_free_pba_store->write_async(to_store_lsn(lsn), b, nullptr,
                                  [](int64_t, sisl::io_blob& b, homestore::logdev_key, void*) { b.buf_free(); });
}

void HomeStateMachineStore::get_free_pba_records(repl_lsn_t start_lsn, repl_lsn_t end_lsn,
                                                 const std::function< void(repl_lsn_t, const pba_list_t&) >& cb) {
    m_free_pba_store->foreach (to_store_lsn(start_lsn),
                               [end_lsn, &cb](store_lsn_t lsn, const homestore::log_buffer& entry) -> bool {
                                   auto rlsn = to_repl_lsn(lsn);
                                   bool ret = (rlsn < end_lsn - 1);
                                   if (rlsn < end_lsn) {
                                       pba_list_t plist;
                                       uint32_t num_pbas = *(r_cast< uint32_t* >(entry.bytes()));
                                       pba_t* raw_ptr = r_cast< pba_t* >(entry.bytes() + sizeof(uint32_t));
                                       for (uint32_t i{0}; i < num_pbas; ++i) {
                                           plist.push_back(*raw_ptr);
                                           ++raw_ptr;
                                       }
                                       cb(rlsn, plist);
                                   }
                                   return ret;
                               });
}

void HomeStateMachineStore::remove_free_pba_records_upto(repl_lsn_t lsn) {
    m_free_pba_store->truncate(to_store_lsn(lsn));
    m_last_write_lsn.store(0);
}

void HomeStateMachineStore::flush_free_pba_records() {
    auto last_lsn = m_last_write_lsn.load();
    m_free_pba_store->flush_sync(last_lsn == 0 ? homestore::invalid_lsn() : to_store_lsn(last_lsn));
}

// TODO: PENDING CHECKPOINT AND FLUSH CODE
} // namespace home_replication