#include "chrome/browser/enterprise/signals/signals_aggregator_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/device_signals/core/browser/signals_aggregator.h"

namespace enterprise_signals {

SignalsAggregatorFactory* SignalsAggregatorFactory::GetInstance() {
  return nullptr;
}
device_signals::SignalsAggregator*
SignalsAggregatorFactory::GetForProfile(Profile* profile) {
  return nullptr;
}

}  // namespace enterprise_signals
