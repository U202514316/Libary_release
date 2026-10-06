using UnrealBuildTool;

public class Libary_ReleaseEditor : ModuleRules
{
    public Libary_ReleaseEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Libary_Release", "AssetRegistry", "UnrealEd", "UMG", "UMGEditor", "BlueprintGraph", "KismetCompiler", "Kismet", "Slate", "SlateCore", "SlateRHIRenderer", "ImageWrapper", "RenderCore", "RHI", "Json" });
    }
}
