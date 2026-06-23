#include "chrome/browser/enterprise/idle/dialog_manager.h"

namespace enterprise_idle {

DialogManager::DialogManager() = default;
DialogManager::~DialogManager() = default;
base::CallbackListSubscription DialogManager::MaybeShowDialog(
    Profile* profile,
    base::TimeDelta threshold,
    const base::flat_set<ActionType>& action_types,
    FinishedCallback on_finished) {
  return base::CallbackListSubscription();
}

}  // namespace enterprise_idle
