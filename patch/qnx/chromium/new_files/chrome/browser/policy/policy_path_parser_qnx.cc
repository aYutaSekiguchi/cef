// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/policy_path_parser.h"

namespace policy {

namespace path_parser {

base::FilePath::StringType ExpandPathVariables(
    const base::FilePath::StringType& untranslated_string) {
  return untranslated_string;
}

}  // namespace path_parser

}  // namespace policy
