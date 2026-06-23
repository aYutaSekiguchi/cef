#include "chrome/browser/enterprise/signals/system_signals_service_host_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/device_signals/core/browser/system_signals_service_host.h"

namespace enterprise_signals {

SystemSignalsServiceHostFactory* SystemSignalsServiceHostFactory::GetInstance() {
  return nullptr;
}
device_signals::SystemSignalsServiceHost*
SystemSignalsServiceHostFactory::GetForProfile(Profile* profile) {
  return nullptr;
}

}  // namespace enterprise_signals
