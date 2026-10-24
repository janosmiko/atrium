// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_autofill_bubble_handler.cc
#include "chrome/browser/ui/atrium/atrium_autofill_bubble_handler.h"

#include <concepts>
#include <memory>

#include "chrome/browser/ui/autofill/autofill_ai/autofill_ai_import_data_controller.h"
#include "chrome/browser/ui/autofill/autofill_bubble_base.h"
#include "chrome/browser/ui/autofill/payments/save_card_ui.h"
#include "chrome/browser/ui/autofill/payments/save_iban_ui.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/views/autofill/address_sign_in_promo_view.h"
#include "chrome/browser/ui/views/autofill/autofill_ai/autofill_ai_import_data_bubble_view.h"
#include "chrome/browser/ui/views/autofill/autofill_ai/autofill_ai_local_save_notification_view.h"
#include "chrome/browser/ui/views/autofill/autofill_location_bar_bubble.h"
#include "chrome/browser/ui/views/autofill/payments/filled_card_information_bubble_views.h"
#include "chrome/browser/ui/views/autofill/payments/manage_saved_iban_bubble_view.h"
#include "chrome/browser/ui/views/autofill/payments/mandatory_reauth_confirmation_bubble_view.h"
#include "chrome/browser/ui/views/autofill/payments/mandatory_reauth_opt_in_bubble_view.h"
#include "chrome/browser/ui/views/autofill/payments/offer_notification_bubble_views.h"
#include "chrome/browser/ui/views/autofill/payments/save_card_bubble_views.h"
#include "chrome/browser/ui/views/autofill/payments/save_card_manage_cards_bubble_views.h"
#include "chrome/browser/ui/views/autofill/payments/save_card_offer_bubble_views.h"
#include "chrome/browser/ui/views/autofill/payments/save_iban_bubble_view.h"
#include "chrome/browser/ui/views/autofill/payments/save_payment_method_and_virtual_card_enroll_confirmation_bubble_views.h"
#include "chrome/browser/ui/views/autofill/payments/virtual_card_enroll_bubble_views.h"
#include "chrome/browser/ui/views/autofill/save_address_profile_view.h"
#include "chrome/browser/ui/views/autofill/update_address_profile_view.h"
#include "chrome/browser/ui/views/bubble_anchor_util_views.h"
#include "chrome/browser/ui/views/location_bar/location_bar_bubble_delegate_view.h"
#include "components/autofill/core/browser/ui/payments/virtual_card_enroll_bubble_controller.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"

namespace atrium {

namespace {

// Same shape as AutofillBubbleHandlerImpl's anonymous-namespace ShowBubble<>
// (chrome/browser/ui/views/autofill/autofill_bubble_handler_impl.cc), minus
// the ToolbarButtonProvider/action-id anchor lookup: a chromeless window has
// no per-action toolbar buttons to anchor to, so every bubble anchors to
// whatever `anchor_provider` resolves to (the active content pane, or an
// unanchored bubble if there is none - see the class comment).
template <typename View, typename... Args>
  requires(std::derived_from<View, autofill::AutofillLocationBarBubble>)
View* ShowBubble(
    const AtriumAutofillBubbleHandler::AnchorProvider& anchor_provider,
    ui::ElementIdentifier highlight_element,
    bool is_user_gesture,
    Args&&... args) {
  views::BubbleAnchor const anchor_view = anchor_provider.Run();
  auto bubble =
      std::make_unique<View>(anchor_view, std::forward<Args>(args)...);
  // An unanchored bubble (no active pane - see GetAutofillBubbleAnchor) has
  // no anchor_widget() for CreateBubbleWidget to fall back to, and
  // has_parent() defaults true, so DCHECK(bubble_params.parent ||
  // !bubble->has_parent()) in CreateBubbleWidget would fire. Tell the
  // bubble it's intentionally parentless, same as caption_bubble.cc and the
  // ChromeOS Mako/Lobster bubbles do for the same reason.
  if (anchor_view.IsNull()) {
    bubble->set_has_parent(false);
  }
  // Handle anchors for icon highlighting.
  if (!bubble_anchor_util::IsHighlightable(anchor_view)) {
    bubble->SetHighlightedElement(highlight_element);
  }

  View* const bubble_ptr = bubble.get();
  views::BubbleDialogDelegateView::CreateBubble(std::move(bubble));
  const LocationBarBubbleDelegateView::DisplayReason display_reason =
      is_user_gesture ? LocationBarBubbleDelegateView::USER_GESTURE
                      : LocationBarBubbleDelegateView::AUTOMATIC;
  if constexpr (requires { bubble_ptr->Show(display_reason); }) {
    bubble_ptr->Show(display_reason);
  } else {
    bubble_ptr->ShowForReason(display_reason);
  }
  return bubble_ptr;
}

template <typename View, typename... Args>
  requires std::derived_from<View, autofill::AutofillLocationBarBubble>
View* ShowAddressProfileBubble(
    const AtriumAutofillBubbleHandler::AnchorProvider& anchor_provider,
    bool is_user_gesture,
    Args&&... args) {
  return ShowBubble<View>(anchor_provider, kAutofillAddressPageActionElementId,
                          is_user_gesture, std::forward<Args>(args)...);
}

}  // namespace

AtriumAutofillBubbleHandler::AtriumAutofillBubbleHandler(
    AnchorProvider anchor_provider)
    : anchor_provider_(std::move(anchor_provider)) {}

AtriumAutofillBubbleHandler::~AtriumAutofillBubbleHandler() = default;

autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowSaveCreditCardBubble(
    content::WebContents* web_contents,
    autofill::SaveCardBubbleController* controller,
    bool is_user_gesture) {
  switch (controller->GetPaymentsBubbleType()) {
    case autofill::PaymentsBubbleType::kLocalSave:
    case autofill::PaymentsBubbleType::kLocalCvcSave:
    case autofill::PaymentsBubbleType::kUploadSave:
    case autofill::PaymentsBubbleType::kUploadCvcSave:
    case autofill::PaymentsBubbleType::kUploadInProgress:
      return ShowBubble<autofill::SaveCardOfferBubbleViews>(
          anchor_provider_, kAutofillSavePaymentsPageActionElementId,
          is_user_gesture, web_contents, controller);
    case autofill::PaymentsBubbleType::kManageCards:
      return ShowBubble<autofill::SaveCardManageCardsBubbleViews>(
          anchor_provider_, kAutofillSavePaymentsPageActionElementId,
          is_user_gesture, web_contents, controller);
    case autofill::PaymentsBubbleType::kUploadComplete:
    case autofill::PaymentsBubbleType::kInactive:
      break;
  }
  NOTREACHED();
}

autofill::AutofillBubbleBase* AtriumAutofillBubbleHandler::ShowIbanBubble(
    content::WebContents* web_contents,
    autofill::IbanBubbleController* controller,
    bool is_user_gesture,
    autofill::IbanBubbleType bubble_type) {
  switch (bubble_type) {
    case autofill::IbanBubbleType::kLocalSave:
    case autofill::IbanBubbleType::kUploadSave:
    case autofill::IbanBubbleType::kUploadInProgress:
      return ShowBubble<autofill::SaveIbanBubbleView>(
          anchor_provider_, kAutofillSavePaymentsPageActionElementId,
          is_user_gesture, web_contents, controller);
    case autofill::IbanBubbleType::kManageSavedIban:
      return ShowBubble<autofill::ManageSavedIbanBubbleView>(
          anchor_provider_, kAutofillSavePaymentsPageActionElementId,
          is_user_gesture, web_contents, controller);
    case autofill::IbanBubbleType::kUploadCompleted:
    case autofill::IbanBubbleType::kInactive:
      break;
  }
  NOTREACHED();
}

autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowOfferNotificationBubble(
    content::WebContents* web_contents,
    autofill::OfferNotificationBubbleController* controller,
    bool is_user_gesture) {
  return ShowBubble<autofill::OfferNotificationBubbleViews>(
      anchor_provider_, kOfferNotificationChipElementId, is_user_gesture,
      web_contents, controller);
}

autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowSaveAddressProfileBubble(
    content::WebContents* web_contents,
    std::unique_ptr<autofill::SaveAddressBubbleController> controller,
    bool is_user_gesture) {
  return ShowAddressProfileBubble<autofill::SaveAddressProfileView>(
      anchor_provider_, is_user_gesture, std::move(controller), web_contents);
}

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowAddressSignInPromo(
    content::WebContents* web_contents,
    const autofill::AutofillProfile& autofill_profile) {
  views::BubbleAnchor anchor = anchor_provider_.Run();
  autofill::AddressSignInPromoView* bubble =
      new autofill::AddressSignInPromoView(anchor, web_contents,
                                           autofill_profile);
  // See ShowBubble<>'s comment: an unanchored bubble must be marked
  // parentless to avoid tripping CreateBubbleWidget's DCHECK.
  if (anchor.IsNull()) {
    bubble->set_has_parent(false);
  }
  if (!bubble_anchor_util::IsHighlightable(anchor)) {
    bubble->SetHighlightedElement(kAutofillAddressPageActionElementId);
  }

  views::BubbleDialogDelegateView::CreateBubble(bubble);
  bubble->ShowForReason(LocationBarBubbleDelegateView::AUTOMATIC);
  return bubble;
}
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowSaveAutofillAiDataBubble(
    content::WebContents* web_contents,
    autofill::AutofillAiImportDataController* controller) {
  return ShowBubble<autofill::AutofillAiImportDataBubbleView>(
      anchor_provider_, kAutofillAddressPageActionElementId,
      /*is_user_gesture=*/false, web_contents, controller);
}

autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowAutofillAiLocalSaveNotification(
    content::WebContents* web_contents,
    autofill::AutofillAiImportDataController* controller) {
  return ShowBubble<autofill::AutofillAiLocalSaveNotificationView>(
      anchor_provider_, kAutofillAddressPageActionElementId,
      /*is_user_gesture=*/false, web_contents, controller);
}

autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowUpdateAddressProfileBubble(
    content::WebContents* web_contents,
    std::unique_ptr<autofill::UpdateAddressBubbleController> controller,
    bool is_user_gesture) {
  return ShowAddressProfileBubble<autofill::UpdateAddressProfileView>(
      anchor_provider_, is_user_gesture, std::move(controller), web_contents);
}

autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowFilledCardInformationBubble(
    content::WebContents* web_contents,
    autofill::FilledCardInformationBubbleController* controller,
    bool is_user_gesture) {
  return ShowBubble<autofill::FilledCardInformationBubbleViews>(
      anchor_provider_, kAutofillFilledCardInformationPageActionElementId,
      is_user_gesture, web_contents, controller);
}

autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowVirtualCardEnrollBubble(
    content::WebContents* web_contents,
    autofill::VirtualCardEnrollBubbleController* controller,
    bool is_user_gesture) {
  views::BubbleAnchor anchor = anchor_provider_.Run();
  autofill::VirtualCardEnrollBubbleViews* bubble =
      new autofill::VirtualCardEnrollBubbleViews(anchor, web_contents,
                                                 controller);
  // See ShowBubble<>'s comment: an unanchored bubble must be marked
  // parentless to avoid tripping CreateBubbleWidget's DCHECK.
  if (anchor.IsNull()) {
    bubble->set_has_parent(false);
  }

  views::BubbleDialogDelegateView::CreateBubble(bubble);

  // VirtualCardEnrollBubbleController::IsEnrollmentInProgress() == true when
  // the bubble has been accepted and the enrollment is still in progress. In
  // this case we do not want to offer enrollment again on reshow.
  if (controller->IsEnrollmentInProgress()) {
    bubble->SwitchToLoadingState();
  }

  bubble->ShowForReason(
      is_user_gesture ? autofill::VirtualCardEnrollBubbleViews::USER_GESTURE
                      : autofill::VirtualCardEnrollBubbleViews::AUTOMATIC);
  if (!bubble_anchor_util::IsHighlightable(anchor)) {
    bubble->SetHighlightedElement(
        kAutofillVirtualCardEnrollPageActionElementId);
  }

  return bubble;
}

autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowVirtualCardEnrollConfirmationBubble(
    content::WebContents* web_contents,
    autofill::VirtualCardEnrollBubbleController* controller) {
  views::BubbleAnchor anchor = anchor_provider_.Run();
  base::OnceCallback<void(autofill::PaymentsUiClosedReason)> callback =
      controller->GetOnBubbleClosedCallback();
  const autofill::SavePaymentMethodAndVirtualCardEnrollConfirmationUiParams&
      ui_params = controller->GetConfirmationUiParams();

  return ShowSaveCardAndVirtualCardEnrollConfirmationBubble(
      anchor, web_contents, std::move(callback),
      kAutofillVirtualCardEnrollPageActionElementId, ui_params);
}

autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowMandatoryReauthBubble(
    content::WebContents* web_contents,
    autofill::MandatoryReauthBubbleController* controller,
    bool is_user_gesture,
    autofill::MandatoryReauthBubbleType bubble_type) {
  auto anchor = anchor_provider_.Run();

  switch (bubble_type) {
    case autofill::MandatoryReauthBubbleType::kOptIn: {
      autofill::MandatoryReauthOptInBubbleView* bubble =
          new autofill::MandatoryReauthOptInBubbleView(anchor, web_contents,
                                                        controller);
      // See ShowBubble<>'s comment: an unanchored bubble must be marked
      // parentless to avoid tripping CreateBubbleWidget's DCHECK.
      if (anchor.IsNull()) {
        bubble->set_has_parent(false);
      }
      bubble->SetHighlightedElement(
          kAutofillMandatoryReauthPageActionElementId);
      views::BubbleDialogDelegateView::CreateBubble(bubble);
      bubble->Show(is_user_gesture
                       ? LocationBarBubbleDelegateView::USER_GESTURE
                       : LocationBarBubbleDelegateView::AUTOMATIC);
      return bubble;
    }
    case autofill::MandatoryReauthBubbleType::kConfirmation: {
      autofill::MandatoryReauthConfirmationBubbleView* bubble =
          new autofill::MandatoryReauthConfirmationBubbleView(
              anchor, web_contents, controller);
      // See ShowBubble<>'s comment: an unanchored bubble must be marked
      // parentless to avoid tripping CreateBubbleWidget's DCHECK.
      if (anchor.IsNull()) {
        bubble->set_has_parent(false);
      }
      bubble->SetHighlightedElement(
          kAutofillMandatoryReauthPageActionElementId);
      views::BubbleDialogDelegateView::CreateBubble(bubble);
      bubble->Show(is_user_gesture
                       ? LocationBarBubbleDelegateView::USER_GESTURE
                       : LocationBarBubbleDelegateView::AUTOMATIC);
      return bubble;
    }
    case autofill::MandatoryReauthBubbleType::kInactive:
      NOTREACHED();
  }
}

autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowSaveCardConfirmationBubble(
    content::WebContents* web_contents,
    autofill::SaveCardBubbleController* controller) {
  views::BubbleAnchor anchor = anchor_provider_.Run();
  base::OnceCallback<void(autofill::PaymentsUiClosedReason)> callback =
      controller->GetOnBubbleClosedCallback();

  const autofill::SavePaymentMethodAndVirtualCardEnrollConfirmationUiParams&
      ui_params = controller->GetConfirmationUiParams();

  return ShowSaveCardAndVirtualCardEnrollConfirmationBubble(
      anchor, web_contents, std::move(callback),
      kAutofillSavePaymentsPageActionElementId, ui_params);
}

autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowSaveIbanConfirmationBubble(
    content::WebContents* web_contents,
    autofill::IbanBubbleController* controller) {
  views::BubbleAnchor anchor = anchor_provider_.Run();
  base::OnceCallback<void(autofill::PaymentsUiClosedReason)> callback =
      controller->GetOnBubbleClosedCallback();

  return ShowSaveCardAndVirtualCardEnrollConfirmationBubble(
      anchor, web_contents, std::move(callback),
      kAutofillSavePaymentsPageActionElementId,
      controller->GetConfirmationUiParams());
}

// ShowOmniboxAutofillBubble and ShowPaymentsChurnedUsersBubble are new
// AutofillBubbleHandler pure virtuals in 151; Atrium has no omnibox-anchored
// or churned-users bubble surface yet, so these are minimal no-op stubs.
autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowOmniboxAutofillBubble(
    content::WebContents* web_contents,
    autofill::OmniboxAutofillBubbleController* controller) {
  return nullptr;
}

autofill::AutofillBubbleBase*
AtriumAutofillBubbleHandler::ShowPaymentsChurnedUsersBubble(
    content::WebContents* web_contents,
    autofill::PaymentsChurnedUsersBubbleController* controller,
    bool is_user_gesture) {
  return nullptr;
}

autofill::AutofillBubbleBase* AtriumAutofillBubbleHandler::
    ShowSaveCardAndVirtualCardEnrollConfirmationBubble(
        views::BubbleAnchor anchor,
        content::WebContents* web_contents,
        base::OnceCallback<void(autofill::PaymentsUiClosedReason)>
            controller_hide_callback,
        ui::ElementIdentifier highlight_element,
        autofill::SavePaymentMethodAndVirtualCardEnrollConfirmationUiParams
            ui_params) {
  autofill::SavePaymentMethodAndVirtualCardEnrollConfirmationBubbleViews*
      bubble =
          new autofill::SavePaymentMethodAndVirtualCardEnrollConfirmationBubbleViews(
              anchor, web_contents, std::move(controller_hide_callback),
              std::move(ui_params));

  // See ShowBubble<>'s comment: an unanchored bubble has no anchor_widget()
  // for CreateBubbleWidget to fall back to, so it must be marked parentless
  // to avoid tripping CreateBubbleWidget's DCHECK.
  if (anchor.IsNull()) {
    bubble->set_has_parent(false);
  }

  if (!bubble_anchor_util::IsHighlightable(anchor)) {
    bubble->SetHighlightedElement(highlight_element);
  }
  views::BubbleDialogDelegateView::CreateBubble(bubble);
  bubble->ShowForReason(LocationBarBubbleDelegateView::AUTOMATIC);

  return bubble;
}

}  // namespace atrium
