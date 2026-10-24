// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_autofill_bubble_handler.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_AUTOFILL_BUBBLE_HANDLER_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_AUTOFILL_BUBBLE_HANDLER_H_

#include <memory>

#include "base/functional/callback.h"
#include "chrome/browser/ui/autofill/autofill_bubble_handler.h"
#include "components/autofill/core/browser/ui/payments/payments_ui_closed_reasons.h"
#include "components/autofill/core/browser/ui/payments/save_payment_method_and_virtual_card_enroll_confirmation_ui_params.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/views/bubble/bubble_anchor.h"

namespace content {
class WebContents;
}  // namespace content

namespace autofill {
class AutofillBubbleBase;
class AutofillProfile;
class AutofillAiImportDataController;
class FilledCardInformationBubbleController;
class SaveCardBubbleController;
class IbanBubbleController;
class OfferNotificationBubbleController;
class SaveAddressBubbleController;
class UpdateAddressBubbleController;
class MandatoryReauthBubbleController;
class VirtualCardEnrollBubbleController;
enum class IbanBubbleType;
enum class MandatoryReauthBubbleType;
}  // namespace autofill

namespace atrium {

// Real (non-null) autofill::AutofillBubbleHandler for chromeless Atrium
// windows. Transcribed method-for-method from AutofillBubbleHandlerImpl
// (chrome/browser/ui/views/autofill/autofill_bubble_handler_impl.{h,cc}) -
// that class's only dependency on ToolbarButtonProvider is
// GetBubbleAnchor(), which this class replaces with `anchor_provider`: a
// chromeless window has no toolbar and therefore never registers a
// ToolbarButtonProvider (see AtriumBrowserWindow::GetAutofillBubbleAnchor,
// which resolves the anchor from the window's active content pane, falling
// back to an unanchored views::BubbleAnchor() - same fallback
// sharing_hub_window_controller.cc uses for the same reason, see its
// ShowSharingHubBubble comment).
//
// Known limitations:
//  - No page-action icon exists in a chromeless window, so a bubble that
//    would normally collapse to an icon on dismiss has nowhere to re-open
//    from. SetHighlightedElement() calls are kept (harmless no-op without an
//    icon to highlight) so the transcription stays mechanical, but the
//    re-open-from-icon affordance itself does not exist here.
//  - Save/enroll flows that round-trip to Google Payments servers are
//    unverified without a signed-in profile and API keys; this class only
//    covers bubble construction and anchoring, not server-side behavior.
class AtriumAutofillBubbleHandler : public autofill::AutofillBubbleHandler {
 public:
  // Returns the views::BubbleAnchor to anchor the next bubble to. Called
  // once per Show*Bubble() call (never cached) since the active pane can
  // change between calls.
  using AnchorProvider = base::RepeatingCallback<views::BubbleAnchor()>;

  explicit AtriumAutofillBubbleHandler(AnchorProvider anchor_provider);

  AtriumAutofillBubbleHandler(const AtriumAutofillBubbleHandler&) = delete;
  AtriumAutofillBubbleHandler& operator=(const AtriumAutofillBubbleHandler&) =
      delete;

  ~AtriumAutofillBubbleHandler() override;

  // autofill::AutofillBubbleHandler:
  autofill::AutofillBubbleBase* ShowSaveCreditCardBubble(
      content::WebContents* web_contents,
      autofill::SaveCardBubbleController* controller,
      bool is_user_gesture) override;
  autofill::AutofillBubbleBase* ShowIbanBubble(
      content::WebContents* web_contents,
      autofill::IbanBubbleController* controller,
      bool is_user_gesture,
      autofill::IbanBubbleType bubble_type) override;
  autofill::AutofillBubbleBase* ShowOfferNotificationBubble(
      content::WebContents* web_contents,
      autofill::OfferNotificationBubbleController* controller,
      bool is_user_gesture) override;
  autofill::AutofillBubbleBase* ShowSaveAutofillAiDataBubble(
      content::WebContents* web_contents,
      autofill::AutofillAiImportDataController* controller) override;
  autofill::AutofillBubbleBase* ShowAutofillAiLocalSaveNotification(
      content::WebContents* web_contents,
      autofill::AutofillAiImportDataController* controller) override;
  autofill::AutofillBubbleBase* ShowSaveAddressProfileBubble(
      content::WebContents* web_contents,
      std::unique_ptr<autofill::SaveAddressBubbleController> controller,
      bool is_user_gesture) override;
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  autofill::AutofillBubbleBase* ShowAddressSignInPromo(
      content::WebContents* web_contents,
      const autofill::AutofillProfile& autofill_profile) override;
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)
  autofill::AutofillBubbleBase* ShowUpdateAddressProfileBubble(
      content::WebContents* web_contents,
      std::unique_ptr<autofill::UpdateAddressBubbleController> controller,
      bool is_user_gesture) override;
  autofill::AutofillBubbleBase* ShowFilledCardInformationBubble(
      content::WebContents* web_contents,
      autofill::FilledCardInformationBubbleController* controller,
      bool is_user_gesture) override;
  autofill::AutofillBubbleBase* ShowVirtualCardEnrollBubble(
      content::WebContents* web_contents,
      autofill::VirtualCardEnrollBubbleController* controller,
      bool is_user_gesture) override;
  autofill::AutofillBubbleBase* ShowVirtualCardEnrollConfirmationBubble(
      content::WebContents* web_contents,
      autofill::VirtualCardEnrollBubbleController* controller) override;
  autofill::AutofillBubbleBase* ShowMandatoryReauthBubble(
      content::WebContents* web_contents,
      autofill::MandatoryReauthBubbleController* controller,
      bool is_user_gesture,
      autofill::MandatoryReauthBubbleType bubble_type) override;
  autofill::AutofillBubbleBase* ShowSaveCardConfirmationBubble(
      content::WebContents* web_contents,
      autofill::SaveCardBubbleController* controller) override;
  autofill::AutofillBubbleBase* ShowSaveIbanConfirmationBubble(
      content::WebContents* web_contents,
      autofill::IbanBubbleController* controller) override;
  autofill::AutofillBubbleBase* ShowOmniboxAutofillBubble(
      content::WebContents* web_contents,
      autofill::OmniboxAutofillBubbleController* controller) override;
  autofill::AutofillBubbleBase* ShowPaymentsChurnedUsersBubble(
      content::WebContents* web_contents,
      autofill::PaymentsChurnedUsersBubbleController* controller,
      bool is_user_gesture) override;

 private:
  // Show the save card and virtual card enrollment confirmation bubble.
  autofill::AutofillBubbleBase*
  ShowSaveCardAndVirtualCardEnrollConfirmationBubble(
      views::BubbleAnchor anchor,
      content::WebContents* web_contents,
      base::OnceCallback<void(autofill::PaymentsUiClosedReason)>
          controller_hide_callback,
      ui::ElementIdentifier highlight_element,
      autofill::SavePaymentMethodAndVirtualCardEnrollConfirmationUiParams
          ui_params);

  AnchorProvider anchor_provider_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_AUTOFILL_BUBBLE_HANDLER_H_
