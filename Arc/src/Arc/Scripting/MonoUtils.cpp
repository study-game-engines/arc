#include "arcpch.h"
#include "MonoUtils.h"

#include "ScriptEngine.h"

#include <mono/metadata/object.h>

namespace ArcEngine
{
	bool MonoUtils::CheckMonoError(MonoError& error)
	{
		ARC_PROFILE_SCOPE();

		bool hasError = !mono_error_ok(&error);

		if (hasError)
		{
			uint16_t errorCode = mono_error_get_error_code(&error);
			const char* errorMessage = mono_error_get_message(&error);

			ARC_CORE_ERROR("Mono Error");
			ARC_CORE_ERROR("Error Code: {0}\nError Message: {1}", errorCode, errorMessage);
			mono_error_cleanup(&error);
			return true;
		}

		return false;
	}

	eastl::string MonoUtils::MonoStringToUTF8(MonoString* monoString)
	{
		ARC_PROFILE_SCOPE();

		if (monoString == nullptr || mono_string_length(monoString) == 0)
			return "";

		MonoError error;
		char* utf8 = mono_string_to_utf8_checked(monoString, &error);
		if (CheckMonoError(error))
			return "";

		eastl::string result(utf8);
		mono_free(utf8);
		return result;
	}

	MonoString* MonoUtils::UTF8ToMonoString(const eastl::string& str)
	{
		ARC_PROFILE_SCOPE();

		return mono_string_new(ScriptEngine::GetDomain(), str.c_str());
	}
}
