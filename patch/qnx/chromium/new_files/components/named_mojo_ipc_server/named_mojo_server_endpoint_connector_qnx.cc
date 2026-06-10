// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/named_mojo_ipc_server/named_mojo_server_endpoint_connector.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <utility>

#include "base/files/file_descriptor_watcher_posix.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "base/threading/sequence_bound.h"
#include "base/memory/weak_ptr.h"
#include "components/named_mojo_ipc_server/connection_info.h"
#include "components/named_mojo_ipc_server/endpoint_options.h"
#include "mojo/public/cpp/platform/platform_channel_server_endpoint.h"

// QNX uses the same POSIX named-socket primitives as Linux (SOCK_STREAM
// bind()/listen()/accept() over a filesystem path) but does not expose
// SO_PEERCRED / struct ucred in <sys/socket.h>. Treat the peer as
// authorized on accept() and only fill |pid| in ConnectionInfo.

namespace named_mojo_ipc_server {
namespace {

class NamedMojoServerEndpointConnectorQnx final
    : public NamedMojoServerEndpointConnector {
 public:
  explicit NamedMojoServerEndpointConnectorQnx(
      const EndpointOptions& options,
      base::SequenceBound<Delegate> delegate)
      : NamedMojoServerEndpointConnector(options, std::move(delegate)) {}
  NamedMojoServerEndpointConnectorQnx(
      const NamedMojoServerEndpointConnectorQnx&) = delete;
  NamedMojoServerEndpointConnectorQnx& operator=(
      const NamedMojoServerEndpointConnectorQnx&) = delete;
  ~NamedMojoServerEndpointConnectorQnx() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  }

 private:
  void OnSocketReady();

  // Overrides for NamedMojoServerEndpointConnector.
  bool TryStart() override;

  mojo::PlatformChannelServerEndpoint server_endpoint_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<base::FileDescriptorWatcher::Controller>
      read_watcher_controller_ GUARDED_BY_CONTEXT(sequence_checker_);

  base::WeakPtrFactory<NamedMojoServerEndpointConnectorQnx> weak_factory_{
      this};
};

void NamedMojoServerEndpointConnectorQnx::OnSocketReady() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(server_endpoint_.is_valid());

  int fd = server_endpoint_.platform_handle().GetFD().get();
  base::ScopedFD connection_fd(HANDLE_EINTR(accept(fd, nullptr, 0)));
  if (!connection_fd.is_valid()) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return;
    }
    PLOG(ERROR) << "accept failed";
    return;
  }

  // QNX has no SO_PEERCRED; only |pid| is available from the listener side.
  auto info = std::make_unique<ConnectionInfo>();
  info->pid = base::GetProcId(getpid());

  mojo::PlatformChannelEndpoint endpoint(
      mojo::PlatformHandle(std::move(connection_fd)));
  if (!endpoint.is_valid()) {
    LOG(ERROR) << "Endpoint is invalid.";
    return;
  }
  delegate_.AsyncCall(&Delegate::OnClientConnected)
      .WithArgs(std::move(endpoint), std::move(info));
}

bool NamedMojoServerEndpointConnectorQnx::TryStart() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  mojo::PlatformChannelServerEndpoint server_endpoint =
      mojo::NamedPlatformChannel({options_.server_name}).TakeServerEndpoint();
  if (!server_endpoint.is_valid()) {
    return false;
  }

  server_endpoint_ = std::move(server_endpoint);

  // QNX cannot introspect peer credentials, so require_same_peer_user is
  // always treated as already satisfied; loosen the socket file mode so
  // any user can connect, mirroring the Linux |!require_same_peer_user|
  // path.
  if (chmod(options_.server_name.c_str(), 0o666) != 0) {
    PLOG(ERROR) << "chmod failed";
    return false;
  }
  read_watcher_controller_ = base::FileDescriptorWatcher::WatchReadable(
      server_endpoint_.platform_handle().GetFD().get(),
      base::BindRepeating(&NamedMojoServerEndpointConnectorQnx::OnSocketReady,
                          weak_factory_.GetWeakPtr()));
  delegate_.AsyncCall(&Delegate::OnServerEndpointCreated);
  return true;
}

}  // namespace

// static
base::SequenceBound<NamedMojoServerEndpointConnector>
NamedMojoServerEndpointConnector::Create(
    scoped_refptr<base::SequencedTaskRunner> io_sequence,
    const EndpointOptions& options,
    base::SequenceBound<Delegate> delegate) {
  return base::SequenceBound<NamedMojoServerEndpointConnectorQnx>(
      io_sequence, options, std::move(delegate));
}

}  // namespace named_mojo_ipc_server
