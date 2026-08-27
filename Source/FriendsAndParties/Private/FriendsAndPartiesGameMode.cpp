#include "FriendsAndPartiesGameMode.h"

#include "SocialShowcasePlayerController.h"

AFriendsAndPartiesGameMode::AFriendsAndPartiesGameMode()
{
	PlayerControllerClass = ASocialShowcasePlayerController::StaticClass();
	DefaultPawnClass = nullptr;
}
