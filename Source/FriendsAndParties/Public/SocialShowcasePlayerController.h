#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SocialShowcasePlayerController.generated.h"

class UUserWidget;

UCLASS(BlueprintType, Blueprintable)
class FRIENDSANDPARTIES_API ASocialShowcasePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASocialShowcasePlayerController();

	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category = "Social Showcase|UI")
	void ShowSocialMenu();

	UFUNCTION(BlueprintCallable, Category = "Social Showcase|UI")
	void HideSocialMenu();

	UFUNCTION(BlueprintCallable, Category = "Social Showcase|UI")
	void ToggleSocialMenu();

	UFUNCTION(BlueprintPure, Category = "Social Showcase|UI")
	bool IsSocialMenuVisible() const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Social Showcase|UI")
	TSubclassOf<UUserWidget> SocialMenuWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Social Showcase|UI")
	bool bShowSocialMenuOnBeginPlay = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Social Showcase|UI")
	bool bRefreshSocialSubsystemOnBeginPlay = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Social Showcase|UI")
	bool bUseGameAndUIInputMode = true;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Social Showcase|UI")
	TObjectPtr<UUserWidget> SocialMenuWidget;

private:
	void ApplyVisibleMenuInputMode();
	void ApplyHiddenMenuInputMode();
};
