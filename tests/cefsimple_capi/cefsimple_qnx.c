// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "tests/cefsimple_capi/simple_app.h"

#include "include/capi/cef_app_capi.h"
#include "include/cef_api_hash.h"
#include "tests/cefsimple_capi/simple_utils.h"

// Entry point function for all processes.
int main(int argc, char* argv[]) {
  // Configure the CEF API version. This must be called before any other CEF
  // API functions.
  cef_api_hash(CEF_API_VERSION, 0);

  // Provide CEF with command-line arguments.
  cef_main_args_t main_args = {};
  main_args.argc = argc;
  main_args.argv = argv;

  // Create the application instance (with 1 reference).
  simple_app_t* app = simple_app_create();
  CHECK(app);

  // Add reference before cef_execute_process. Both cef_execute_process and
  // cef_initialize will take ownership of a reference, so we need 2 total.
  app->app.base.add_ref(&app->app.base);

  // CEF applications have multiple sub-processes (render, GPU, etc) that share
  // the same executable. This function checks the command-line and, if this is
  // a sub-process, executes the appropriate logic.
  int exit_code = cef_execute_process(&main_args, &app->app, NULL);
  if (exit_code >= 0) {
    // The sub-process has completed so return here.
    // cef_execute_process took ownership of one reference.
    // Release only the additional reference we added.
    app->app.base.release(&app->app.base);
    return exit_code;
  }

  // Specify CEF global settings here.
  cef_settings_t settings = {};
  settings.size = sizeof(settings);

  // Initialize the CEF browser process. May return false if initialization
  // fails or if early exit is desired.
  int result = cef_initialize(&main_args, &settings, &app->app, NULL);

  // cef_initialize took ownership of one reference.
  // Release our additional reference.
  app->app.base.release(&app->app.base);

  if (!result) {
    return 1;
  }

  // Run the CEF message loop. This will block until cef_quit_message_loop() is
  // called.
  cef_run_message_loop();

  // Shut down CEF.
  cef_shutdown();

  return 0;
}
