#include "SocialShowcasePlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "SocialSubsystem.h"
#include "UObject/ConstructorHelpers.h"

ASocialShowcasePlayerController::ASocialShowcasePlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	static ConstructorHelpers::FClassFinder<UUserWidget> SocialMenuWidgetBPClass(TEXT("/Game/UI/WBP_SocialMenu"));
	if (SocialMenuWidgetBPClass.Succeeded())
	{
		SocialMenuWidgetClass = SocialMenuWidgetBPClass.Class;
	}
}

void ASocialShowcasePlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (bShowSocialMenuOnBeginPlay)
	{
		ShowSocialMenu();
	}

	if (bRefreshSocialSubsystemOnBeginPlay)
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (USocialSubsystem* SocialSubsystem = GameInstance->GetSubsystem<USocialSubsystem>())
			{
				//SocialSubsystem->ConnectWebSocket();
				//SocialSubsystem->RefreshAll();
			}
		}
	}
	
}

void ASocialShowcasePlayerController::ShowSocialMenu()
{
	if (!IsLocalController())
	{
		return;
	}

	if (!SocialMenuWidget && SocialMenuWidgetClass)
	{
		SocialMenuWidget = CreateWidget<UUserWidget>(this, SocialMenuWidgetClass);
	}

	if (!SocialMenuWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("SocialMenuWidgetClass is not set on %s"), *GetName());
		ApplyVisibleMenuInputMode();
		return;
	}

	if (!SocialMenuWidget->IsInViewport())
	{
		SocialMenuWidget->AddToViewport();
	}

	ApplyVisibleMenuInputMode();
}

void ASocialShowcasePlayerController::HideSocialMenu()
{
	if (SocialMenuWidget && SocialMenuWidget->IsInViewport())
	{
		SocialMenuWidget->RemoveFromParent();
	}

	ApplyHiddenMenuInputMode();
}

void ASocialShowcasePlayerController::ToggleSocialMenu()
{
	if (IsSocialMenuVisible())
	{
		HideSocialMenu();
		return;
	}

	ShowSocialMenu();
}

bool ASocialShowcasePlayerController::IsSocialMenuVisible() const
{
	return SocialMenuWidget && SocialMenuWidget->IsInViewport();
}

void ASocialShowcasePlayerController::ApplyVisibleMenuInputMode()
{
	bShowMouseCursor = true;

	if (bUseGameAndUIInputMode)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		if (SocialMenuWidget)
		{
			InputMode.SetWidgetToFocus(SocialMenuWidget->TakeWidget());
		}
		SetInputMode(InputMode);
		return;
	}

	FInputModeUIOnly InputMode;
	if (SocialMenuWidget)
	{
		InputMode.SetWidgetToFocus(SocialMenuWidget->TakeWidget());
	}
	SetInputMode(InputMode);
}

void ASocialShowcasePlayerController::ApplyHiddenMenuInputMode()
{
	bShowMouseCursor = false;

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
}
