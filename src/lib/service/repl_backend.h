#pragma once
#include <memory>
#include <home_replication/repl_decls.h>
namespace nuraft {
class log_store;
}

namespace home_replication {
class ReplicationService;
class StateMachineStore;

class ReplicationServiceBackend {
public:
    ReplicationServiceBackend(ReplicationService* svc);
    virtual ~ReplicationServiceBackend() = default;
    virtual std::shared_ptr< StateMachineStore > create_state_store(uuid_t uuid) = 0;
    virtual std::shared_ptr< nuraft::log_store > create_log_store() = 0;

protected:
    ReplicationService* m_svc;
};
} // namespace home_replication