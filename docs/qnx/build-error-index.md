# QNX Build Error Index

This document is the entry point for looking up prior QNX build and test failures.

Do not keep long fix narratives here. The source of truth for individual incidents is the structured catalog under `docs/qnx/history/build-errors/`.

## What to read first

1. `status.md` for the current validated baseline and accepted exclusions.
2. `build-and-toolchain.md` for bootstrap and toolchain behavior.
3. `testing.md` for QEMU and test-runner usage.
4. `history/build-errors/` for concrete prior incidents.

## How to search

Start from the current failure's pipeline stage:

- `bootstrap`
- `gn`
- `compile`
- `link`
- `package`
- `test`

Then narrow by cause class:

- `build-graph`
- `feature-guard`
- `platform-api-gap`
- `runtime-assumption`
- `test-environment`
- `toolchain-config`
- `type-trait-template`

Search with the smallest stable signature you have:

- exact error code
- header name
- symbol name
- syscall or API name
- target or subsystem name

Examples:

```bash
rg -n "posix_spawnp|EBADF|launch_qnx" docs/qnx/history/build-errors
rg -n "F_GETFL|TakeError::kUnexpectedReadOnlyFd" docs/qnx/history/build-errors
rg -n "fieldtrial_to_struct|--platform=qnx" docs/qnx/history/build-errors
rg -n "open_memstream|libmemstream|makedev" docs/qnx/history/build-errors/link
rg -n "libdrm_qnx_memstream_makedev|xf86drm.c|patches failed to apply" docs/qnx/history/build-errors/bootstrap
rg -n "linux/prctl.h|pthread_setname_np|platform_thread_types" docs/qnx/history/build-errors
rg -n "sys/prctl.h|NativeCPUContext|Port\." docs/qnx/history/build-errors
rg -n "suid_sandbox_client|compile_suid_client|sandbox/policy:policy" docs/qnx/history/build-errors
rg -n "sandbox/linux:sandbox_services|sys/syscall.h|DT_LNK|network_sandbox_hook" docs/qnx/history/build-errors
rg -n "crashpad_is_linux|linux/futex.h|compat/linux/sys/mman|features.h" docs/qnx/history/build-errors
rg -n "Unhandled OS type|VMSize|define kOS|address_types.h" docs/qnx/history/build-errors
rg -n "IOV_MAX|METRICS_OS_NAME|uuid.cc|InitializeWithNew|Port\." docs/qnx/history/build-errors
rg -n "drop_privileges|close_multiple|symbolic_constants_posix|kFDDir|OPEN_MAX|kSignalNames" docs/qnx/history/build-errors
rg -n "fx_qnx_impl|fx_linux_impl|Included on the wrong platform|pdfium" docs/qnx/history/build-errors
rg -n "keycode_converter|SCREEN_PROPERTY_SCAN|sys/usbcodes|Unsupported platform|DOM_CODE" docs/qnx/history/build-errors
rg -n "named_mojo_server_endpoint_connector|require_same_peer_user|SO_PEERCRED|ConnectionInfo::credentials" docs/qnx/history/build-errors
rg -n "client_filterable_state|Study::PLATFORM|PLATFORM_QNX|Unknown platform" docs/qnx/history/build-errors
rg -n "ipcz/reference_drivers/random|getrandom|DevUrandom" docs/qnx/history/build-errors
rg -n "kQnxShmHandle|QnxShmMemory|QnxShmHandle|wrapped_file_descriptor" docs/qnx/history/build-errors
rg -n "IPCZ_MEMFD_QNX_SKIP_IMPL|MFD_ALLOW_SEALING|MultiprocessMemory" docs/qnx/history/build-errors
rg -n "rust_bindgen_generator|mojo_c_system_bindings|Endian not defined" docs/qnx/history/build-errors
rg -n "SKIA_USE_DAWN|skia_use_dawn|gpu_info_collector|USE_DAWN" docs/qnx/history/build-errors
rg -n "gpu_test_config|GetCurrentOS|unknown os|kOsLinux" docs/qnx/history/build-errors
rg -n "DisplayEGL|DrmFourCCFormatToGLInternalFormat|angle_tests" docs/qnx/history/build-errors
rg -n "crashpad_client.h|capture_context.h|NativeCPUContext" docs/qnx/history/build-errors
rg -n "crtn.o|GNU-stack|no-warn-execstack|fatal-warnings" docs/qnx/history/build-errors
rg -n "corrupt patch at line|hunk body shorter than header|trailing empty line" docs/qnx/history/build-errors/bootstrap
rg -n "mojo_webui_version_ts_qnx|is_qnx.*mojo|version.mojom-webui" docs/qnx/history/build-errors/bootstrap
rg -n "FontCache::DeviceScaleFactor|font_platform_data.cc" docs/qnx/history/build-errors/compile
rg -n "kServiceSandbox|ScreenAIServiceFactory|kScreenAI|conditional_features|RemoveDisabledDefinitions" docs/qnx/history/build-errors/compile
rg -n "screen_ai_features_qnx_service|sandbox_mojom_kScreenAI|mojom_gni_is_qnx" docs/qnx/history/build-errors/compile
rg -n "kSizesNeededForShortcutCreation|GetOsSpecificSizes|icon_badging" docs/qnx/history/build-errors/compile
rg -n "only virtual member functions can be marked 'override'|GetFirstPartyActions|sharing_hub_bubble_controller_desktop_impl" docs/qnx/history/build-errors/compile
rg -n "unterminated function-like macro invocation|kIsolatedAppScheme|SetIsolationData|web_app.cc" docs/qnx/history/build-errors/compile
rg -n "tensorflow/compiler/mlir/lite/allocation.h|read_aloud_app_model|dependency_parser_model|build_with_tflite_lib" docs/qnx/history/build-errors/compile
rg -n "NSSDecryptor not implemented|firefox_importer|nss_decryptor|USE_NSS_CERTS" docs/qnx/history/build-errors/compile
rg -n "value_or\(\{\}\)|couldn't infer template argument '_Up'|full_card_request" docs/qnx/history/build-errors/compile
rg -n "std::from_range|flat_hash_set<std::string>|deduced conflicting types for parameter 'InputIter'" docs/qnx/history/build-errors/compile
rg -n "Unsupported target architecture|LaunchDateAndTimeSettings|security_interstitials/content/utils.cc" docs/qnx/history/build-errors/compile
rg -n "ZygoteStarting|ZygoteForked|USE_ZYGOTE|use_zygote|content_main_runner_impl" docs/qnx/history/build-errors/compile
rg -n "Unsupported platform|navigator_base|GetReducedNavigatorPlatform" docs/qnx/history/build-errors/compile
rg -n "Unsupported platform|extensions/common/command|CommandPlatform" docs/qnx/history/build-errors/compile
rg -n "DIR_USER_NATIVE_MESSAGING|DIR_NATIVE_MESSAGING|launch_context_posix|chrome_paths_linux" docs/qnx/history/build-errors/compile
rg -n "cef/grit/cef_resources.h|enable_cef|gen/cef/grit" docs/qnx/history/build-errors/bootstrap
rg -n "cef/grit/cef_resources.h|about_ui.cc|about:impl|cef_resources" docs/qnx/history/build-errors/compile
rg -n "print_preview_dialog_controller|print.mojom.h|ENABLE_PRINT_PREVIEW|chrome_content_browser_client" docs/qnx/history/build-errors/compile
rg -n "webrtc_log_uploader|Platform not supported|Chrome_Linux|GetLogUploadProduct" docs/qnx/history/build-errors/compile
rg -n "memory_details|ZygoteHost|IsZygotePid|USE_ZYGOTE" docs/qnx/history/build-errors/compile
rg -n "web_app_dialogs|web_app_dialog_utils|passwords_private_delegate_impl|IS_CHROMEOS" docs/qnx/history/build-errors/compile
rg -n "kChromeUIProfileCustomization|kChromeUIManagedUserProfileNotice|webui_url_constants|signin_view_controller_delegate_views" docs/qnx/history/build-errors/compile
rg -n "kGlicOnboardingCompleted|kSplitViewCreated|event_constants|feature_engagement::events" docs/qnx/history/build-errors/compile
rg -n "kCreateShortcut|ShowWebAppSettings|WebAppInstallFlow|browser_command_controller" docs/qnx/history/build-errors/compile
rg -n "ShowModalHistorySyncOptInDialog|signin_view_controller|history_sync_optin_service" docs/qnx/history/build-errors/compile
rg -n "kProfileCreationFrictionReductionExperimentSkipCustomizeProfile|kSignInPromoMaterialNextUI|CreateSyncHistoryOptInDelegate|kEnableSupervisedUserVersionSignOutDialog" docs/qnx/history/build-errors/compile
rg -n "ScopedTabbedBrowserDisplayer|download_commands|browser_displayer" docs/qnx/history/build-errors/compile
rg -n "translation_dispatcher_on_device|ENABLE_ON_DEVICE_TRANSLATION|translator.mojom.h|live_translate_controller_factory" docs/qnx/history/build-errors/compile
rg -n "kEnterpriseShortcutsPolicyList|new_tab_page_util|ntp_tiles" docs/qnx/history/build-errors/compile
rg -n "kGuest|switch_utils|chrome_switches" docs/qnx/history/build-errors/compile
rg -n "IDS_PASSWORD_MANAGER_FILLING_REAUTH|password_credential_ui_controller|FILLING_REAUTH" docs/qnx/history/build-errors/compile
rg -n "SiteSettingsHandler|SendZoomLevels|HandleRemoveZoomLevel|kIsolatedAppScheme" docs/qnx/history/build-errors/compile
```

If the immediate search misses:

1. search `docs/qnx/history/` for older handoffs or research
2. search `docs/qnx/history/archive/` for older notes
3. only then broaden to generic source search in the tree

## Directory map

- `docs/qnx/history/build-errors/` — structured incident notes; this is the main lookup surface
- `docs/qnx/history/handoffs/` — larger implementation-area handoff notes
- `docs/qnx/history/research/` — focused investigations and platform analysis
- `docs/qnx/history/archive/` — older notes preserved for reference when newer structured notes are not enough

## Recording new incidents

When a blocker is understood well enough to reuse later:

1. classify it by `stage/category`
2. create a note under `docs/qnx/history/build-errors/<stage>/<category>/`
3. use `.agents/skills/build-breakage-loop/scripts/scaffold_error_note.py` if you want a normalized template

Do not recreate a long running diary file. Add or update the specific structured note instead.
