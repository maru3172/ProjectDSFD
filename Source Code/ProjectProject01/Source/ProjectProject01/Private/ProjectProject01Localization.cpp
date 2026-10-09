// File: Source/ProjectProject01/Private/ProjectProject01Localization.cpp

#include "ProjectProject01Localization.h"

#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"

namespace ProjectProject01Localization
{
	const FName TableId(TEXT("ProjectProject01UI"));
}

FText FProjectProject01Localization::Text(const FString& SourceOrKey)
{
	const FStringTableConstPtr Table = FStringTableRegistry::Get().FindStringTable(
		ProjectProject01Localization::TableId);
	FString Source;
	if (Table.IsValid() && Table->GetSourceString(FTextKey(SourceOrKey), Source))
	{
		return FText::FromStringTable(ProjectProject01Localization::TableId, FTextKey(SourceOrKey));
	}
	return FText::FromString(SourceOrKey);
}

bool FProjectProject01Localization::SetCulture(const FString& CultureName)
{
	return !CultureName.IsEmpty() && FInternationalization::Get().SetCurrentCulture(CultureName);
}

FString FProjectProject01Localization::GetCulture()
{
	return FInternationalization::Get().GetCurrentCulture()->GetName();
}
