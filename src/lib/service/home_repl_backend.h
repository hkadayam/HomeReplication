#pragma once
#include <sisl/fds/buffer.hpp>
#include "service/repl_backend.h"
namespace nuraft {
class log_store;
}

namespace home_replication {
class StateMachineStore;

class HomeReplicationBackend : public ReplicationServiceBackend {
public:
    HomeReplicationBackend(ReplicationService* svc);
    virtual ~HomeReplicationBackend() = default;

    std::shared_ptr< StateMachineStore > create_state_store(uuid_t uuid) override;
    std::shared_ptr< nuraft::log_store > create_log_store() override;

private:
    void rs_super_blk_found(const sisl::byte_view& buf, void* meta_cookie);
};
} // namespace home_replication