// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FriendsAndParties : ModuleRules
{
	public FriendsAndParties(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG" });

		PrivateDependencyModuleNames.AddRange(new string[] { "OnlineSystems", "Slate", "SlateCore" });
		

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
