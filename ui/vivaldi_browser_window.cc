// Copyright (c) 2017 Vivaldi Technologies AS. All rights reserved.
//
// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/vivaldi_browser_window.h"

#include <memory>
#include <string>

#include "app/vivaldi_constants.h"
#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "browser/menus/vivaldi_menus.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/devtools/devtools_contents_resizing_strategy.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/extensions/window_controller.h"
#include "chrome/browser/sessions/session_tab_helper.h"
#include "chrome/browser/ui/apps/chrome_app_delegate.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/browser_window_state.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/download/download_in_progress_dialog_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "components/web_modal/web_contents_modal_dialog_manager_delegate.h"
#include "content/browser/browser_plugin/browser_plugin_guest.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/site_instance.h"
#include "extensions/api/tabs/tabs_private_api.h"
#include "extensions/browser/app_window/native_app_window.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_web_contents_observer.h"
#include "extensions/browser/guest_view/web_view/web_view_guest.h"
#include "extensions/browser/view_type_utils.h"
#include "extensions/common/extension_messages.h"
#include "extensions/helper/vivaldi_app_helper.h"
#include "extensions/schema/devtools_private.h"
#include "extensions/schema/window_private.h"
#include "prefs/vivaldi_pref_names.h"
#include "ui/devtools/devtools_connector.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/vivaldi_app_window_client.h"

#if defined(OS_WIN)
#include "chrome/browser/win/jumplist_factory.h"
#endif

using web_modal::WebContentsModalDialogManager;
using extensions::AppWindow;
using extensions::NativeAppWindow;

namespace extensions {

VivaldiAppWindowContentsImpl::VivaldiAppWindowContentsImpl(
    VivaldiBrowserWindow* host,
    VivaldiWindowEventDelegate* delegate,
    AppDelegate* app_delegate)
    : host_(host),
      is_blocking_requests_(false),
      is_window_ready_(false),
      delegate_(delegate),
      load_timeout_(true, false),
      app_delegate_(app_delegate) {
}

VivaldiAppWindowContentsImpl::~VivaldiAppWindowContentsImpl() { }

void VivaldiAppWindowContentsImpl::Initialize(
    content::BrowserContext* context,
    content::RenderFrameHost* creator_frame,
    const GURL& url) {
  url_ = url;

  scoped_refptr<content::SiteInstance> site_instance =
      content::SiteInstance::CreateForURL(context, url);

  content::RenderProcessHost* process = site_instance->GetProcess();
  content::WebContents::CreateParams create_params(context,
                                                   site_instance.get());
  create_params.opener_render_process_id = process->GetID();
  create_params.opener_render_frame_id =
      creator_frame ? creator_frame->GetRoutingID() : 0;

  web_contents_.reset(content::WebContents::Create(create_params));

  WebContentsObserver::Observe(web_contents_.get());
  web_contents_->GetMutableRendererPrefs()
      ->browser_handles_all_top_level_requests = true;
  web_contents_->GetRenderViewHost()->SyncRendererPrefs();
  web_contents_->SetDelegate(this);

  helper_.reset(new AppWebContentsHelper(context, host_->extension()->id(),
                                         web_contents_.get(), app_delegate_));
}

void VivaldiAppWindowContentsImpl::LoadContents(int32_t creator_process_id) {
  // If the new view is in the same process as the creator, block the created
  // RVH from loading anything until the background page has had a chance to
  // do any initialization it wants. If it's a different process, the new RVH
  // shouldn't communicate with the background page anyway (e.g. sandboxed).
  if (web_contents_->GetMainFrame()->GetProcess()->GetID() ==
      creator_process_id) {
  } else {
    VLOG(1) << "AppWindow created in new process ("
      << web_contents_->GetMainFrame()->GetProcess()->GetID()
      << ") != creator (" << creator_process_id << "). Routing disabled.";
  }

  web_contents_->GetController().LoadURL(
    url_, content::Referrer(), ui::PAGE_TRANSITION_LINK,
    std::string());

  // TODO(pettern): Fix background.
//  OnWindowReady();

  // Ensure we force show the window after some time no matter what.
  load_timeout_.Start(FROM_HERE, base::TimeDelta::FromMilliseconds(5000),
    base::Bind(&VivaldiAppWindowContentsImpl::ForceShowWindow,
                   base::Unretained(this)));
}

void VivaldiAppWindowContentsImpl::NativeWindowChanged(
    NativeAppWindow* native_app_window) {
  StateData old_state = state_;
  StateData new_state;
  new_state.is_fullscreen_ = native_app_window->IsFullscreenOrPending();
  new_state.is_minimized_ = native_app_window->IsMinimized();
  new_state.is_maximized_ = native_app_window->IsMaximized();

  // Call the delegate so it can dispatch events to the js side
  // for any change in state.
  if (old_state.is_fullscreen_ != new_state.is_fullscreen_)
    delegate_->OnFullscreenChanged(new_state.is_fullscreen_);
  if (old_state.is_minimized_ != new_state.is_minimized_ &&
      new_state.is_minimized_)
    delegate_->OnMinimized();
  if (old_state.is_maximized_ != new_state.is_maximized_ &&
      new_state.is_maximized_)
    delegate_->OnMaximized();

  // do not send restore when in fullscreen (VB-976)
  if ((old_state.is_fullscreen_ && !new_state.is_fullscreen_) ||
      (old_state.is_minimized_ && !new_state.is_minimized_) ||
      (!old_state.is_fullscreen_ &&
       (old_state.is_maximized_ && !new_state.is_maximized_))) {
    delegate_->OnRestored();
  }
  state_ = new_state;
}

void VivaldiAppWindowContentsImpl::NativeWindowClosed() {
  load_timeout_.Stop();
  web_contents_.reset(nullptr);
}

void VivaldiAppWindowContentsImpl::OnWindowReady() {
  is_window_ready_ = true;
  if (is_blocking_requests_) {
    is_blocking_requests_ = false;
    web_contents_->GetMainFrame()->ResumeBlockedRequestsForFrame();
  }
}

content::WebContents* VivaldiAppWindowContentsImpl::GetWebContents() const {
  return web_contents_.get();
}

WindowController* VivaldiAppWindowContentsImpl::GetWindowController() const {
  return nullptr;
}

void VivaldiAppWindowContentsImpl::HandleKeyboardEvent(
    content::WebContents* source,
    const content::NativeWebKeyboardEvent& event) {
  host_->HandleKeyboardEvent(event);
}

bool VivaldiAppWindowContentsImpl::PreHandleGestureEvent(
    content::WebContents* source,
    const blink::WebGestureEvent& event) {
  // NOTE(arnar@vivaldi.com): Disable pinch touchscreen zoom for windows/linux
#if defined(OS_WIN) || OS_LINUX
  return blink::WebInputEvent::IsPinchGestureEventType(event.GetType());
#else
  return false;
#endif
}

content::ColorChooser* VivaldiAppWindowContentsImpl::OpenColorChooser(
    content::WebContents* web_contents,
    SkColor initial_color,
    const std::vector<content::ColorSuggestion>& suggestions) {
  return chrome::ShowColorChooser(web_contents, initial_color);
}

void VivaldiAppWindowContentsImpl::RunFileChooser(
    content::RenderFrameHost* render_frame_host,
    const content::FileChooserParams& params) {
  app_delegate_->RunFileChooser(render_frame_host, params);
}

void VivaldiAppWindowContentsImpl::NavigationStateChanged(
    content::WebContents* source,
    content::InvalidateTypes changed_flags) {
  if (changed_flags &
      (content::INVALIDATE_TYPE_TAB | content::INVALIDATE_TYPE_TITLE)) {
    host_->UpdateTitleBar();
  }
}

void VivaldiAppWindowContentsImpl::RequestMediaAccessPermission(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    const content::MediaResponseCallback& callback) {
  DCHECK_EQ(web_contents_.get(), web_contents);
  helper_->RequestMediaAccessPermission(request, callback);
}

bool VivaldiAppWindowContentsImpl::CheckMediaAccessPermission(
    content::WebContents* web_contents,
    const GURL& security_origin,
    content::MediaStreamType type) {
  DCHECK_EQ(web_contents_.get(), web_contents);
  return helper_->CheckMediaAccessPermission(security_origin, type);
}


void VivaldiAppWindowContentsImpl::RenderViewCreated(
    content::RenderViewHost* render_view_host) {
  // An incognito profile is not initialized with the UI zoom value. Set it up
  // here by reading prefs from the regular profile. At this point we do not
  // know the partition key (see ChromeZoomLevelPrefs::InitHostZoomMap) so we
  // just test all keys until we match kVivaldiAppId.
  if (host_->GetProfile()->IsOffTheRecord()) {
    PrefService* pref_service =
        host_->GetProfile()->GetOriginalProfile()->GetPrefs();
    const base::DictionaryValue* dictionaries =
        pref_service->GetDictionary(prefs::kPartitionPerHostZoomLevels);
    bool match = false;
    for (base::DictionaryValue::Iterator it1(*dictionaries);
         !it1.IsAtEnd() && !match; it1.Advance()) {
      // it1.key() refers to the partition key
      const base::DictionaryValue* dictionary = nullptr;
      if (dictionaries->GetDictionary(it1.key(), &dictionary)) {
        // Test each entry (host) until we match the kVivaldiAppId
        for (base::DictionaryValue::Iterator it2(*dictionary);
             !it2.IsAtEnd(); it2.Advance()) {
          if (it2.key() == ::vivaldi::kVivaldiAppId) {
            match = true;
            double zoom_level = 0;
            if (it2.value().GetAsDouble(&zoom_level)) {
              content::HostZoomMap* zoom_map =
                content::HostZoomMap::GetForWebContents(this->GetWebContents());
              DCHECK(zoom_map);
              zoom_map->SetZoomLevelForHost(::vivaldi::kVivaldiAppId,
                                            zoom_level);
            }
            break;
          }
        }
      }
    }
  }
}

bool VivaldiAppWindowContentsImpl::OnMessageReceived(
    const IPC::Message& message,
    content::RenderFrameHost* sender) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_WITH_PARAM(VivaldiAppWindowContentsImpl, message,
                                   sender)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_UpdateDraggableRegions,
                        UpdateDraggableRegions)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void VivaldiAppWindowContentsImpl::ReadyToCommitNavigation(
  content::NavigationHandle* handle) {
  if (!is_window_ready_)
    host_->OnReadyToCommitFirstNavigation();
}

void VivaldiAppWindowContentsImpl::UpdateDraggableRegions(
    content::RenderFrameHost* sender,
    const std::vector<DraggableRegion>& regions) {
  // Only process events for the main frame.
  if (!sender->GetParent()) {
    host_->UpdateDraggableRegions(regions);
  }
}

void VivaldiAppWindowContentsImpl::SuspendRenderFrameHost(
  content::RenderFrameHost* rfh) {
  DCHECK(rfh);
  // Don't bother blocking requests if the renderer side is already good to go.
  if (is_window_ready_)
    return;
  is_blocking_requests_ = true;
  rfh->BlockRequestsForFrame();
}

void VivaldiAppWindowContentsImpl::DidFinishLoad(
    content::RenderFrameHost* render_frame_host,
    const GURL& validated_url) {
  host_->UpdateTitleBar();
  delegate_->OnDocumentLoaded();
  load_timeout_.Stop();
}

void VivaldiAppWindowContentsImpl::ForceShowWindow() {
  delegate_->OnDocumentLoaded();
  load_timeout_.Stop();
}

}  // namespace extensions

// VivaldiBrowserWindow --------------------------------------------------------

VivaldiBrowserWindow::VivaldiBrowserWindow() {
}

VivaldiBrowserWindow::~VivaldiBrowserWindow() {
  // Explicitly set browser_ to NULL.
  browser_.reset();
}

void VivaldiBrowserWindow::Init(Browser* browser,
                                std::string& base_url,
                                extensions::AppWindow::CreateParams& params) {
  browser_.reset(browser);
  base_url_ = base_url;
  params_ = params;

  if (browser) {
    chrome::GetSavedWindowBoundsAndShowState(browser,
                                             &params_.content_spec.bounds,
                                             &initial_state_);
    params_.state = initial_state_;

    // No need to delay creating the web contents, we have a browser.
    CreateWebContents(nullptr);
  }
}

// static
VivaldiBrowserWindow* VivaldiBrowserWindow::GetBrowserWindowForBrowser(
    const Browser* browser) {
  return static_cast<VivaldiBrowserWindow*>(browser->window());
}

// static
extensions::AppWindow::CreateParams
VivaldiBrowserWindow::PrepareWindowParameters(Browser* browser,
                                              std::string& base_url) {
  extensions::AppWindow::CreateParams params;

  base_url = "browser.html";
  params.frame = extensions::AppWindow::FRAME_NONE;
  params.window_spec.minimum_size = gfx::Size(500, 300);

  // if browser is nullptr, we expect the callee to set the defaults.
  if (browser && browser->profile()->GetPrefs()->GetBoolean(
    vivaldiprefs::kVivaldiUseNativeWindowDecoration)) {
    params.frame = extensions::AppWindow::FRAME_CHROME;
  }
  return params;
}

// static
VivaldiBrowserWindow* VivaldiBrowserWindow::CreateVivaldiBrowserWindow(
    Browser* browser,
    std::string& base_url,
    extensions::AppWindow::CreateParams& params) {
  VivaldiBrowserWindow* window = new VivaldiBrowserWindow();
  window->Init(browser, base_url, params);
  return window;
}

void VivaldiBrowserWindow::UpdateDraggableRegions(
    const std::vector<extensions::DraggableRegion>& regions) {
  native_app_window_->UpdateDraggableRegions(regions);
}

void VivaldiBrowserWindow::OnReadyToCommitFirstNavigation() {
  // TODO(pettern): Might need this implemented.
}

void VivaldiBrowserWindow::CreateWebContents(content::RenderFrameHost* host) {
#if defined(OS_WIN)
  JumpListFactory::GetForProfile(browser_->profile());
#endif
  extension_ = const_cast<extensions::Extension*>(
      extensions::ExtensionRegistry::Get(browser_->profile())
          ->GetExtensionById(vivaldi::kVivaldiAppId,
                             extensions::ExtensionRegistry::EVERYTHING));
  DCHECK(extension_);

  GURL url = extension_->GetResourceURL(base_url_);

  // TODO(pettern): Make a VivaldiAppDelegate, remove patches in
  // ChromeAppDelegate.
  app_delegate_.reset(new ChromeAppDelegate(false));
  app_window_contents_.reset(new extensions::VivaldiAppWindowContentsImpl(
      this, this, app_delegate_.get()));

  // We should always be set as vivaldi in Browser.
  DCHECK(browser_->is_vivaldi());

  app_window_contents_->Initialize(browser_->profile(), host, url);

  content::WebContentsObserver::Observe(web_contents());
  SetViewType(web_contents(), extensions::VIEW_TYPE_APP_WINDOW);
  app_delegate_->InitWebContents(web_contents());

  extensions::ExtensionWebContentsObserver::GetForWebContents(web_contents())
      ->dispatcher()
      ->set_delegate(this);

  web_modal::WebContentsModalDialogManager::CreateForWebContents(
    web_contents());

  web_modal::WebContentsModalDialogManager::FromWebContents(web_contents())
    ->SetDelegate(this);

  extensions::VivaldiAppHelper::CreateForWebContents(web_contents());
  zoom::ZoomController::CreateForWebContents(web_contents());

  VivaldiAppWindowClient::Set(VivaldiAppWindowClient::GetInstance());

  VivaldiAppWindowClient* app_window_client = VivaldiAppWindowClient::Get();
  native_app_window_.reset(
      app_window_client->CreateNativeAppWindow(this, &params_));

  // TODO(pettern): Crashes on shutdown, fix.
  // extensions::ExtensionRegistry::Get(browser_->profile())->AddObserver(this);

  app_window_contents_->LoadContents(params_.creator_process_id);
}

content::WebContents* VivaldiBrowserWindow::web_contents() const {
  if (app_window_contents_)
    return app_window_contents_->GetWebContents();
  return nullptr;
}

void VivaldiBrowserWindow::OnExtensionUnloaded(
  content::BrowserContext* browser_context,
  const extensions::Extension* extension,
  extensions::UnloadedExtensionReason reason) {
  if (vivaldi::kVivaldiAppId == extension->id())
    native_app_window_->Close();
}

void VivaldiBrowserWindow::Show() {
#if !defined(OS_WIN)
  // The Browser associated with this browser window must become the active
  // browser at the time |Show()| is called. This is the natural behavior under
  // Windows and Ash, but other platforms will not trigger
  // OnWidgetActivationChanged() until we return to the runloop. Therefore any
  // calls to Browser::GetLastActive() will return the wrong result if we do not
  // explicitly set it here.
  // A similar block also appears in BrowserWindowCocoa::Show().
  BrowserList::SetLastActive(browser());
#endif

  // We delay showing it until the UI document has loaded.
  if (!has_loaded_document_)
    return;

  Show(extensions::AppWindow::SHOW_ACTIVE);
}

void VivaldiBrowserWindow::Show(extensions::AppWindow::ShowType show_type) {
  app_delegate_->OnShow();
  is_hidden_ = false;

  if (initial_state_ == ui::SHOW_STATE_FULLSCREEN)
    SetFullscreen(true);
  else if (initial_state_ == ui::SHOW_STATE_MAXIMIZED)
    Maximize();
  else if (initial_state_ == ui::SHOW_STATE_MINIMIZED)
    Minimize();

  switch (show_type) {
    case extensions::AppWindow::SHOW_ACTIVE:
      GetBaseWindow()->Show();
      break;
    case extensions::AppWindow::SHOW_INACTIVE:
      GetBaseWindow()->ShowInactive();
      break;
  }
  has_been_shown_ = true;

  OnNativeWindowChanged();
}

void VivaldiBrowserWindow::Hide() {
  is_hidden_ = true;
  GetBaseWindow()->Hide();
  app_delegate_->OnHide();
}

void VivaldiBrowserWindow::SetBounds(const gfx::Rect& bounds) {
  if (native_app_window_)
    native_app_window_->SetBounds(bounds);
  return;
}

void VivaldiBrowserWindow::Close() {
  extensions::DevtoolsConnectorAPI* api =
    extensions::DevtoolsConnectorAPI::GetFactoryInstance()->Get(
      browser_->profile());
  DCHECK(api);
  api->CloseDevtoolsForBrowser(browser_.get());

  native_app_window_->Close();
}

void VivaldiBrowserWindow::ConfirmBrowserCloseWithPendingDownloads(
  int download_count,
  Browser::DownloadClosePreventionType dialog_type,
  bool app_modal,
  const base::Callback<void(bool)>& callback) {
#ifdef OS_MACOSX
  // We allow closing the window here since the real quit decision on Mac is made
  // in [AppController quit:].
  callback.Run(true);
#else
  DownloadInProgressDialogView::Show(
    GetNativeWindow(), download_count, dialog_type, app_modal, callback);
#endif  // OS_MACOSX
}

void VivaldiBrowserWindow::Activate() {
  native_app_window_->Activate();
  BrowserList::SetLastActive(browser_.get());
}

void VivaldiBrowserWindow::Deactivate() {}

bool VivaldiBrowserWindow::IsActive() const {
  return native_app_window_ ? native_app_window_->IsActive() : false;
}

bool VivaldiBrowserWindow::IsAlwaysOnTop() const {
  return false;
}

gfx::NativeWindow VivaldiBrowserWindow::GetNativeWindow() const {
  return GetBaseWindow()->GetNativeWindow();
}

StatusBubble* VivaldiBrowserWindow::GetStatusBubble() {
  return NULL;
}

gfx::Rect VivaldiBrowserWindow::GetRestoredBounds() const {
  if (native_app_window_) {
    return native_app_window_->GetRestoredBounds();
  }
  return gfx::Rect();
}

ui::WindowShowState VivaldiBrowserWindow::GetRestoredState() const {
  if (native_app_window_) {
    return native_app_window_->GetRestoredState();
  }
  return ui::SHOW_STATE_DEFAULT;
}

gfx::Rect VivaldiBrowserWindow::GetBounds() const {
  if (native_app_window_) {
    gfx::Rect bounds = native_app_window_->GetBounds();
    bounds.Inset(native_app_window_->GetFrameInsets());
    return bounds;
  }
  return gfx::Rect();
}

bool VivaldiBrowserWindow::IsMaximized() const {
  if (native_app_window_) {
    return native_app_window_->IsMaximized();
  }
  return false;
}

bool VivaldiBrowserWindow::IsMinimized() const {
  if (native_app_window_) {
    return native_app_window_->IsMinimized();
  }
  return false;
}

void VivaldiBrowserWindow::Maximize() {
  if (native_app_window_) {
    return native_app_window_->Maximize();
  }
}

void VivaldiBrowserWindow::Minimize() {
  if (native_app_window_) {
    return native_app_window_->Minimize();
  }
}

void VivaldiBrowserWindow::Restore() {
  if (IsFullscreen()) {
    native_app_window_->SetFullscreen(
        extensions::AppWindow::FULLSCREEN_TYPE_NONE);
  } else {
    GetBaseWindow()->Restore();
  }
}

bool VivaldiBrowserWindow::ShouldHideUIForFullscreen() const {
  return true;
}

bool VivaldiBrowserWindow::IsFullscreenBubbleVisible() const {
  return false;
}

LocationBar* VivaldiBrowserWindow::GetLocationBar() const {
  return NULL;
}

ToolbarActionsBar* VivaldiBrowserWindow::GetToolbarActionsBar() {
  return NULL;
}

content::KeyboardEventProcessingResult
VivaldiBrowserWindow::PreHandleKeyboardEvent(
    const content::NativeWebKeyboardEvent& event) {
  return content::KeyboardEventProcessingResult::NOT_HANDLED;
}

void VivaldiBrowserWindow::HandleKeyboardEvent(
    const content::NativeWebKeyboardEvent& event) {
  extensions::TabsPrivateAPI* api =
      extensions::TabsPrivateAPI::GetFactoryInstance()->Get(
          browser_->profile());
  api->SendKeyboardShortcutEvent(event);

  native_app_window_->HandleKeyboardEvent(event);
}

bool VivaldiBrowserWindow::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  return vivaldi::VivaldiGetAcceleratorForCommandId(nullptr, command_id,
                                                    accelerator);
}

ui::AcceleratorProvider* VivaldiBrowserWindow::GetAcceleratorProvider() {
  return this;
}

bool VivaldiBrowserWindow::IsBookmarkBarVisible() const {
  return false;
}

bool VivaldiBrowserWindow::IsBookmarkBarAnimating() const {
  return false;
}

bool VivaldiBrowserWindow::IsTabStripEditable() const {
  return true;
}

bool VivaldiBrowserWindow::IsToolbarVisible() const {
  return false;
}

bool VivaldiBrowserWindow::IsDownloadShelfVisible() const {
  return false;
}

DownloadShelf* VivaldiBrowserWindow::GetDownloadShelf() {
  return NULL;
}

// See comments on: BrowserWindow.VivaldiShowWebSiteSettingsAt.
void VivaldiBrowserWindow::VivaldiShowWebsiteSettingsAt(
    Profile* profile,
    content::WebContents* web_contents,
    const GURL& url,
    const security_state::SecurityInfo& security_info,
    gfx::Point pos) {
#if defined(USE_AURA)
  // This is only for AURA.  Mac is done in VivaldiBrowserCocoa.
  PageInfoBubbleView::ShowPopupAtPos(pos, profile, web_contents, url,
                                           security_info, browser_.get(),
                                           GetNativeWindow());
#endif
}

WindowOpenDisposition VivaldiBrowserWindow::GetDispositionForPopupBounds(
    const gfx::Rect& bounds) {
  return WindowOpenDisposition::NEW_POPUP;
}

FindBar* VivaldiBrowserWindow::CreateFindBar() {
  return nullptr;
}

int VivaldiBrowserWindow::GetRenderViewHeightInsetWithDetachedBookmarkBar() {
  return 0;
}

void VivaldiBrowserWindow::ExecuteExtensionCommand(
    const extensions::Extension* extension,
    const extensions::Command& command) {}

ExclusiveAccessContext* VivaldiBrowserWindow::GetExclusiveAccessContext() {
  return this;
}

autofill::SaveCardBubbleView* VivaldiBrowserWindow::ShowSaveCreditCardBubble(
    content::WebContents* contents,
    autofill::SaveCardBubbleController* controller,
    bool is_user_gesture) {
  return NULL;
}

void VivaldiBrowserWindow::DestroyBrowser() {
  // TODO(pettern): Crashes on shutdown, fix.
//  extensions::ExtensionRegistry::Get(browser_->profile())->RemoveObserver(this);
  browser_.reset();
}

gfx::Size VivaldiBrowserWindow::GetContentsSize() const {
  if (native_app_window_) {
    // TODO(pettern): This is likely not correct, should be tab contents.
    gfx::Rect bounds = native_app_window_->GetBounds();
    bounds.Inset(native_app_window_->GetFrameInsets());
    return bounds.size();
  }
  return gfx::Size();
}

std::string VivaldiBrowserWindow::GetWorkspace() const {
  return std::string();
}

bool VivaldiBrowserWindow::IsVisibleOnAllWorkspaces() const {
  return false;
}

Profile* VivaldiBrowserWindow::GetProfile() {
  return browser_->profile();
}

void VivaldiBrowserWindow::UpdateExclusiveAccessExitBubbleContent(
    const GURL& url,
    ExclusiveAccessBubbleType bubble_type,
    ExclusiveAccessBubbleHideCallback bubble_first_hide_callback) {}

void VivaldiBrowserWindow::OnExclusiveAccessUserInput() {}

content::WebContents* VivaldiBrowserWindow::GetActiveWebContents() {
  return browser_->tab_strip_model()->GetActiveWebContents();
}

void VivaldiBrowserWindow::UnhideDownloadShelf() {}

void VivaldiBrowserWindow::HideDownloadShelf() {}

ShowTranslateBubbleResult VivaldiBrowserWindow::ShowTranslateBubble(
    content::WebContents* contents,
    translate::TranslateStep step,
    translate::TranslateErrors::Type error_type,
    bool is_user_gesture) {
  return ShowTranslateBubbleResult::BROWSER_WINDOW_NOT_VALID;
}

void VivaldiBrowserWindow::UpdateDevTools() {
  TabStripModel* tab_strip_model = browser_->tab_strip_model();

  // Get the docking state.
  const base::DictionaryValue* prefs =
      browser_->profile()->GetPrefs()->GetDictionary(
          prefs::kDevToolsPreferences);

  std::string docking_state;
  std::string device_mode;

  // DevToolsWindow code has already activated the tab.
  content::WebContents* contents = tab_strip_model->GetActiveWebContents();
  int tab_id = SessionTabHelper::IdForTab(contents);
  extensions::DevtoolsConnectorAPI* api =
    extensions::DevtoolsConnectorAPI::GetFactoryInstance()->Get(
      browser_->profile());
  DCHECK(api);
  scoped_refptr<extensions::DevtoolsConnectorItem> item =
      make_scoped_refptr(api->GetOrCreateDevtoolsConnectorItem(tab_id));

  // Iterate the list of inspected tabs and send events if any is
  // in the process of closing.
  for (int i = 0; i < tab_strip_model->count(); ++i) {
    WebContents* inspected_contents = tab_strip_model->GetWebContentsAt(i);
    DevToolsWindow* w =
        DevToolsWindow::GetInstanceForInspectedWebContents(inspected_contents);
    if (w && w->IsClosing()) {
      int id = SessionTabHelper::IdForTab(inspected_contents);
      std::unique_ptr<base::ListValue> args =
        extensions::vivaldi::devtools_private::OnClosed::Create(id);

      extensions::DevtoolsConnectorAPI::BroadcastEvent(
        extensions::vivaldi::devtools_private::OnClosed::kEventName,
        std::move(args), browser_->profile());

      ResetDockingState(id);
    }
  }
  DevToolsWindow* window =
      DevToolsWindow::GetInstanceForInspectedWebContents(contents);

  if (window) {
    // We handle the closing devtools windows above.
    if (!window->IsClosing()) {
      if (prefs->GetString("currentDockState", &docking_state)) {
        // Strip quotation marks from the state.
        base::ReplaceChars(docking_state, "\"", "", &docking_state);
        if (item->docking_state() != docking_state) {
          item->set_docking_state(docking_state);

          std::unique_ptr<base::ListValue> args =
              extensions::vivaldi::devtools_private::OnDockingStateChanged::
                  Create(tab_id, docking_state);

          extensions::DevtoolsConnectorAPI::BroadcastEvent(
            extensions::vivaldi::devtools_private::OnDockingStateChanged::
            kEventName,
            std::move(args), browser_->profile());
        }
      }
      if (prefs->GetString("showDeviceMode", &device_mode)) {
        base::ReplaceChars(device_mode, "\"", "", &device_mode);
        bool device_mode_enabled = device_mode == "true";
        if (item->device_mode_enabled() == device_mode_enabled) {
          item->set_device_mode_enabled(device_mode_enabled);
        }
      }
    }
  }
}

void VivaldiBrowserWindow::ResetDockingState(int tab_id) {
  extensions::DevtoolsConnectorAPI* api =
    extensions::DevtoolsConnectorAPI::GetFactoryInstance()->Get(
      browser_->profile());
  DCHECK(api);
  scoped_refptr<extensions::DevtoolsConnectorItem> item =
      make_scoped_refptr(api->GetOrCreateDevtoolsConnectorItem(tab_id));

  item->ResetDockingState();

  std::unique_ptr<base::ListValue> args =
    extensions::vivaldi::devtools_private::OnDockingStateChanged::Create(
      tab_id, item->docking_state());

  extensions::DevtoolsConnectorAPI::BroadcastEvent(
    extensions::vivaldi::devtools_private::OnDockingStateChanged::
    kEventName,
    std::move(args),
    browser_->profile());
}

bool VivaldiBrowserWindow::IsToolbarShowing() const {
  return false;
}

NativeAppWindow* VivaldiBrowserWindow::GetBaseWindow() const {
  return native_app_window_.get();
}

void VivaldiBrowserWindow::OnNativeWindowChanged() {
  // This may be called during Init before |native_app_window_| is set.
  if (!native_app_window_)
    return;

  if (native_app_window_) {
    native_app_window_->UpdateEventTargeterWithInset();
  }

  if (app_window_contents_)
    app_window_contents_->NativeWindowChanged(native_app_window_.get());
}

void VivaldiBrowserWindow::OnNativeClose() {
  WebContentsModalDialogManager* modal_dialog_manager =
    WebContentsModalDialogManager::FromWebContents(web_contents());
  if (modal_dialog_manager) {
    modal_dialog_manager->SetDelegate(nullptr);
  }
  if (app_window_contents_) {
    app_window_contents_->NativeWindowClosed();
  }

  // For a while we used a direct "delete this" here. That causes the browser_
  // object to be destoyed immediately and that will kill the private profile.
  // The missing profile will (very often) trigger a crash on Linux. VB-34358
  // TODO(all): There is missing cleanup in chromium code. See VB-34358.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
    FROM_HERE,
    base::Bind(&VivaldiBrowserWindow::DeleteThis, base::Unretained(this)));
}

void VivaldiBrowserWindow::DeleteThis() {
  delete this;
}

void VivaldiBrowserWindow::OnNativeWindowActivated() {
  BrowserList::SetLastActive(browser_.get());
}

void VivaldiBrowserWindow::UpdateTitleBar() {
  native_app_window_->UpdateWindowTitle();
  native_app_window_->UpdateWindowIcon();
}

base::string16 VivaldiBrowserWindow::GetTitle() {
  if (!extension_)
    return base::string16();

  // WebContents::GetTitle() will return the page's URL if there's no <title>
  // specified. However, we'd prefer to show the name of the extension in that
  // case, so we directly inspect the NavigationEntry's title.
  base::string16 title;
  content::NavigationEntry* entry = web_contents() ?
    web_contents()->GetController().GetLastCommittedEntry() : nullptr;
  if (!entry || entry->GetTitle().empty()) {
    title = base::UTF8ToUTF16(extension_->name());
  } else {
    title = web_contents()->GetTitle();
  }
  base::RemoveChars(title, base::ASCIIToUTF16("\n"), &title);
  return title;
}

void VivaldiBrowserWindow::OnActiveTabChanged(
    content::WebContents* old_contents,
    content::WebContents* new_contents,
    int index,
    int reason) {
  UpdateTitleBar();

#if !defined(OS_MACOSX)

  // TODO(pettern): MOVE THIS CODE TO ExtensionActionUtil::ActiveTabChanged,
  // this does not belong here.
  //
  // Make sure we do a re-layout to keep all webviews in size-sync. Scenario; if
  // the user switch tab in fullscreen mode, only
  // BrowserPlugin::UpdateVisibility is called. We need to make sure
  // BrowserPlugin::UpdateGeometry is called. Note this was not visible prior to
  // the change we have in BrowserPlugin::UpdateGeometry where a lot of extra
  // resizing on hidden WebViews was done, and we want to be lazy. See VB-29032.
  content::WebContentsImpl *web_contents =
      static_cast<content::WebContentsImpl *>(new_contents);
  content::BrowserPluginGuest *guestplugin =
      web_contents->GetBrowserPluginGuest();
  // Only do this in fullscreen as switching tabs in windowed mode will cause a
  // relayout through RenderWidgetCompositor::UpdateLayerTreeHost() (This is due
  // to painting optimization.)
  bool is_fullscreen_without_chrome = IsFullscreen();
  if (guestplugin && is_fullscreen_without_chrome) {
    gfx::Rect guest_rect = guestplugin->guest_window_rect();
    gfx::Rect bounds = GetBounds();
    gfx::Size window_size = gfx::Size(bounds.width(), bounds.height());
    if (window_size != gfx::Size(guest_rect.x() + guest_rect.width(),
                                 guest_rect.y() + guest_rect.height())) {
      guestplugin->SizeContents(window_size);
    }
  }
#endif // !OS_MACOSX
}

void VivaldiBrowserWindow::SetWebContentsBlocked(
    content::WebContents* web_contents,
    bool blocked) {
  app_delegate_->SetWebContentsBlocked(web_contents, blocked);
}

bool VivaldiBrowserWindow::IsWebContentsVisible(
    content::WebContents* web_contents) {
  return app_delegate_->IsWebContentsVisible(web_contents);
}

web_modal::WebContentsModalDialogHost*
VivaldiBrowserWindow::GetWebContentsModalDialogHost() {
  return native_app_window_.get();
}

void VivaldiBrowserWindow::EnterFullscreen(
    const GURL& url,
    ExclusiveAccessBubbleType bubble_type) {
  SetFullscreen(true);
}

void VivaldiBrowserWindow::ExitFullscreen() {
  SetFullscreen(false);
}

void VivaldiBrowserWindow::SetFullscreen(bool enable) {
  native_app_window_->SetFullscreen(
      enable ? extensions::AppWindow::FULLSCREEN_TYPE_WINDOW_API
             : extensions::AppWindow::FULLSCREEN_TYPE_NONE);
}

bool VivaldiBrowserWindow::IsFullscreen() const {
  return native_app_window_.get() ? native_app_window_->IsFullscreen() : false;
}

extensions::WindowController*
VivaldiBrowserWindow::GetExtensionWindowController() const {
  return app_window_contents_->GetWindowController();
}

content::WebContents* VivaldiBrowserWindow::GetAssociatedWebContents() const {
  return web_contents();
}

content::WebContents* VivaldiBrowserWindow::GetActiveWebContents() const {
  return browser_->tab_strip_model()->GetActiveWebContents();
}

namespace {
void DispatchEvent(Profile* profile,
                   const std::string& event_name,
                   std::unique_ptr<base::ListValue> event_args) {
  extensions::EventRouter* event_router = extensions::EventRouter::Get(profile);
  if (event_router) {
    event_router->BroadcastEvent(base::WrapUnique(
        new extensions::Event(extensions::events::VIVALDI_EXTENSION_EVENT,
                              event_name, std::move(event_args))));
  }
}
}  // namespace

// VivaldiWindowEventDelegate implementation
void VivaldiBrowserWindow::OnMinimized() {
  if (browser_.get() == nullptr) {
    return;
  }
  DispatchEvent(browser_->profile(),
                extensions::vivaldi::window_private::OnMinimized::kEventName,
                extensions::vivaldi::window_private::OnMinimized::Create(
                    browser_->session_id().id()));
}

void VivaldiBrowserWindow::OnMaximized() {
  if (browser_.get() == nullptr) {
    return;
  }
  DispatchEvent(browser_->profile(),
                extensions::vivaldi::window_private::OnMaximized::kEventName,
                extensions::vivaldi::window_private::OnMaximized::Create(
                    browser_->session_id().id()));
}

void VivaldiBrowserWindow::OnRestored() {
  if (browser_.get() == nullptr) {
    return;
  }
  DispatchEvent(browser_->profile(),
                extensions::vivaldi::window_private::OnRestored::kEventName,
                extensions::vivaldi::window_private::OnRestored::Create(
                  browser_->session_id().id()));
}

void VivaldiBrowserWindow::OnFullscreenChanged(bool fullscreen) {
  DispatchEvent(browser_->profile(),
                extensions::vivaldi::window_private::OnFullscreen::kEventName,
                extensions::vivaldi::window_private::OnFullscreen::Create(
                    browser_->session_id().id(), fullscreen));
}

void VivaldiBrowserWindow::OnDocumentLoaded() {
  has_loaded_document_ = true;
  Show();
}
