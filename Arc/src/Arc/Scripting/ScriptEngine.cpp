#include "arcpch.h"
#include "ScriptEngine.h"

#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/object.h>
#include <mono/metadata/attrdefs.h>
#include <mono/metadata/threads.h>
#include <mono/metadata/mono-debug.h>

#ifdef ARC_DEBUG
#include <mono/metadata/debug-helpers.h>
#endif // ARC_DEBUG

#include "MonoUtils.h"
#include "GCManager.h"
#include "ScriptEngineRegistry.h"

namespace ArcEngine
{
	struct ScriptEngineData
	{
		eastl::string CoreAssemblyPath = "../Sandbox/Assemblies/Arc-ScriptCore.dll";
		eastl::string AppAssemblyPath = "../Sandbox/Assemblies/Sandbox.dll";

		MonoDomain* RootDomain = nullptr;
		MonoDomain* AppDomain = nullptr;

		MonoAssembly* CoreAssembly = nullptr;
		MonoAssembly* AppAssembly = nullptr;
		MonoImage* CoreImage = nullptr;
		MonoImage* AppImage = nullptr;

		MonoClass* EntityClass = nullptr;

		eastl::unordered_map<eastl::string, Ref<ScriptClass>> EntityClasses;

		using EntityInstanceMap = eastl::unordered_map<UUID, eastl::unordered_map<eastl::string, ScriptInstance*>>;
		EntityInstanceMap EntityInstances;
		EntityInstanceMap EntityRuntimeInstances;
		EntityInstanceMap* CurrentEntityInstanceMap = nullptr;
	};

	static Ref<ScriptEngineData> s_Data;

	Scene* ScriptEngine::s_CurrentScene = nullptr;

	void ScriptEngine::Init()
	{
		ARC_PROFILE_SCOPE();

		mono_set_dirs("C:/Program Files/Mono/lib",
        "C:/Program Files/Mono/etc");

		s_Data = CreateRef<ScriptEngineData>();

		s_Data->RootDomain = mono_jit_init("ArcJITRuntime");
		ARC_CORE_ASSERT(s_Data->RootDomain);
		mono_debug_domain_create(s_Data->RootDomain);
		mono_thread_set_main(mono_thread_current());

		GCManager::Init();

		ReloadAppDomain();
	}

	void ScriptEngine::Shutdown()
	{
		ARC_PROFILE_SCOPE();

		for (auto [id, script] : s_Data->EntityInstances)
		{
			for (auto& it = script.begin(); it != script.end(); ++it)
			{
				delete it->second;
			}
		}
		s_Data->EntityClasses.clear();

		GCManager::Shutdown();

		mono_jit_cleanup(s_Data->RootDomain);
	}

	void ScriptEngine::LoadCoreAssembly()
	{
		ARC_PROFILE_SCOPE();

		s_Data->CoreAssembly = MonoUtils::LoadMonoAssembly(s_Data->CoreAssemblyPath.c_str());
		s_Data->CoreImage = mono_assembly_get_image(s_Data->CoreAssembly);

		s_Data->EntityClass = mono_class_from_name(s_Data->CoreImage, "ArcEngine", "Entity");

		GCManager::CollectGarbage();
		ScriptEngineRegistry::RegisterAll();
	}

	void ScriptEngine::LoadClientAssembly()
	{
		ARC_PROFILE_SCOPE();

		s_Data->AppAssembly = MonoUtils::LoadMonoAssembly(s_Data->AppAssemblyPath.c_str());
		s_Data->AppImage = mono_assembly_get_image(s_Data->AppAssembly);

		GCManager::CollectGarbage();

		s_Data->EntityClasses.clear();
		LoadAssemblyClasses(s_Data->AppAssembly);
	}

	void ScriptEngine::ReloadAppDomain()
	{
		// Clean old instances
		for (auto [id, script] : s_Data->EntityInstances)
		{
			for (auto& it = script.begin(); it != script.end(); ++it)
			{
				GCManager::ReleaseObjectReference(it->second->GetHandle());
			}
		}

		if (s_Data->AppDomain)
		{
			mono_domain_set(s_Data->RootDomain, true);
			mono_domain_unload(s_Data->AppDomain);
			s_Data->AppDomain = nullptr;
		}

		// Compile Sandbox
		//std::string argument = "C:/Windows/Microsoft.NET/Framework64/v4.0.30319/msbuild.exe -p:Configuration=Debug ../Sandbox/Sandbox.csproj";
		//system(argument.c_str());

		s_Data->AppDomain = mono_domain_create_appdomain("ScriptRuntime", nullptr);
		ARC_CORE_ASSERT(s_Data->AppDomain);
		mono_domain_set(s_Data->AppDomain, true);
		mono_debug_domain_create(s_Data->AppDomain);

		LoadCoreAssembly();
		LoadClientAssembly();

		// Recreate instances
		eastl::unordered_map<UUID, eastl::unordered_map<eastl::string, ScriptInstance*>> entityInstances;
		for (auto [id, script] : s_Data->EntityInstances)
		{
			for (auto [name, scriptInstance] : script)
			{
				auto& scriptClass = s_Data->EntityClasses.at(name);
				if (s_Data->EntityInstances[id][name] != nullptr)
				{
					entityInstances[id][name] = s_Data->EntityInstances[id][name];
					entityInstances[id][name]->Invalidate(scriptClass, id);
				}
				else
				{
					entityInstances[id][name] = new ScriptInstance(scriptClass, id);
				}
			}
		}
		s_Data->EntityInstances = entityInstances;

		s_Data->CurrentEntityInstanceMap = &s_Data->EntityInstances;
	}

	void ArcEngine::ScriptEngine::LoadAssemblyClasses(MonoAssembly* assembly)
	{
		ARC_PROFILE_SCOPE();

		MonoImage* image = mono_assembly_get_image(assembly);
		const MonoTableInfo* typeDefinitionTable = mono_image_get_table_info(image, MONO_TABLE_TYPEDEF);
		int32_t numTypes = mono_table_info_get_rows(typeDefinitionTable);

		for (int32_t i = 0; i < numTypes; ++i)
		{
			uint32_t cols[MONO_TYPEDEF_SIZE];
			mono_metadata_decode_row(typeDefinitionTable, i, cols, MONO_TYPEDEF_SIZE);

			const char* nameSpace = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAMESPACE]);
			const char* name = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAME]);
			eastl::string fullname = fmt::format("{}.{}", nameSpace, name).c_str();

			MonoClass* monoClass = mono_class_from_name(image, nameSpace, name);
			MonoClass* parentClass = mono_class_get_parent(monoClass);
			
			if (parentClass)
			{
				const char* parentName = mono_class_get_name(parentClass);
				const char* parentNamespace = mono_class_get_namespace(parentClass);

				bool isEntity = monoClass && strcmp(parentName, "Entity") == 0 && strcmp(parentNamespace, "ArcEngine") == 0;
				if (isEntity)
					s_Data->EntityClasses[fullname] = CreateRef<ScriptClass>(nameSpace, name);
				
				ARC_CORE_TRACE(fullname.c_str());
			}
		}
	}

	MonoImage* ScriptEngine::GetCoreAssemblyImage()
	{
		return s_Data->CoreImage;
	}

	void ScriptEngine::OnRuntimeBegin()
	{
		s_Data->CurrentEntityInstanceMap = &s_Data->EntityRuntimeInstances;

		for (auto [id, script] : s_Data->EntityInstances)
		{
			for (auto [name, scriptInstance] : script)
			{
				ScriptInstance* copy = new ScriptInstance(scriptInstance, id);
				s_Data->EntityRuntimeInstances[id][name] = copy;
			}
		}
	}

	void ScriptEngine::OnRuntimeEnd()
	{
		s_Data->CurrentEntityInstanceMap = &s_Data->EntityInstances;
		for (auto [id, script] : s_Data->EntityRuntimeInstances)
		{
			for (auto& it = script.begin(); it != script.end(); ++it)
				delete it->second;
		}
		s_Data->EntityRuntimeInstances.clear();
	}

	ScriptInstance* ScriptEngine::CreateInstance(Entity entity, const eastl::string& name)
	{
		auto& scriptClass = s_Data->EntityClasses.at(name);
		UUID entityID = entity.GetUUID();
		ScriptInstance* instance = new ScriptInstance(scriptClass, entityID);
		(*s_Data->CurrentEntityInstanceMap)[entityID][name] = instance;
		return (*s_Data->CurrentEntityInstanceMap)[entityID][name];
	}

	ScriptInstance* ScriptEngine::GetInstance(Entity entity, const eastl::string& name)
	{
		return (*s_Data->CurrentEntityInstanceMap)[entity.GetUUID()].at(name);
	}

	bool ScriptEngine::HasClass(const eastl::string& className)
	{
		return s_Data->EntityClasses.find_as(className) != s_Data->EntityClasses.end();
	}

	void ScriptEngine::RemoveInstance(Entity entity, const eastl::string& name)
	{
		delete (*s_Data->CurrentEntityInstanceMap)[entity.GetUUID()][name];
		(*s_Data->CurrentEntityInstanceMap)[entity.GetUUID()].erase(name);
	}

	MonoDomain* ScriptEngine::GetDomain()
	{
		return s_Data->AppDomain;
	}

	eastl::map<eastl::string, Ref<Field>>& ScriptEngine::GetFields(Entity entity, const char* className)
	{
		return (*s_Data->CurrentEntityInstanceMap)[entity.GetUUID()].at(className)->GetFields();
	}

	eastl::unordered_map<eastl::string, Ref<ScriptClass>>& ScriptEngine::GetClasses()
	{
		return s_Data->EntityClasses;
	}

	void ScriptEngine::SetProperty(GCHandle handle, void* property, void** params)
	{
		ARC_PROFILE_SCOPE();

		MonoMethod* method = mono_property_get_set_method((MonoProperty*)property);
		MonoObject* reference = GCManager::GetReferencedObject(handle);
		if (reference)
			mono_runtime_invoke(method, reference, params, nullptr);
	}

	MonoProperty* ScriptEngine::GetProperty(const char* className, const char* propertyName)
	{
		ARC_PROFILE_SCOPE();

		return s_Data->EntityClasses.at(className)->GetProperty(className, propertyName);
	}

	////////////////////////////////////////////////////////////////////////
	// Script Class ////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////
	ScriptClass::ScriptClass(MonoClass* monoClass)
		: m_MonoClass(monoClass)
	{
		LoadFields();
	}

	ScriptClass::ScriptClass(const eastl::string& classNamespace, const eastl::string& className)
		: m_ClassNamespace(classNamespace), m_ClassName(className)
	{
		m_MonoClass = mono_class_from_name(s_Data->AppImage, classNamespace.c_str(), className.c_str());
		LoadFields();
	}

	GCHandle ScriptClass::Instantiate()
	{
		MonoObject* object = mono_object_new(s_Data->AppDomain, m_MonoClass);
		if (object)
		{
			mono_runtime_object_init(object);
			return GCManager::CreateObjectReference(object, false);
		}

		return nullptr;
	}

	MonoMethod* ScriptClass::GetMethod(const eastl::string& signature)
	{
		eastl::string desc = m_ClassNamespace + "." + m_ClassName + ":" + signature;
		MonoMethodDesc* methodDesc = mono_method_desc_new(desc.c_str(), true);
		return mono_method_desc_search_in_class(methodDesc, m_MonoClass);
	}

	GCHandle ScriptClass::InvokeMethod(GCHandle gcHandle, MonoMethod* method, void** params)
	{
		MonoObject* reference = GCManager::GetReferencedObject(gcHandle);
		mono_runtime_invoke(method, reference, params, nullptr);
		return gcHandle;
	}

	MonoProperty* ScriptClass::GetProperty(const char* className, const char* propertyName)
	{
		ARC_PROFILE_SCOPE();

		eastl::string key = eastl::string(className) + propertyName;
		if (m_Properties.find(key) != m_Properties.end())
			return m_Properties.at(key);

		MonoProperty* property = mono_class_get_property_from_name(m_MonoClass, propertyName);
		if (property)
			m_Properties.emplace(key, property);
		else
			ARC_CORE_ERROR("Property: {0} not found in class {1}", propertyName, className);

		return property;
	}

	void ScriptClass::SetProperty(GCHandle gcHandle, void* property, void** params)
	{
		ARC_PROFILE_SCOPE();

		MonoMethod* method = mono_property_get_set_method((MonoProperty*)property);
		InvokeMethod(gcHandle, method, params);
	}

	void ScriptClass::LoadFields()
	{
		m_Fields.clear();

		MonoClassField* iter;
		void* ptr = 0;
		while ((iter = mono_class_get_fields(m_MonoClass, &ptr)) != NULL)
		{
			const char* propertyName = mono_field_get_name(iter);
			uint32_t flags = mono_field_get_flags(iter);
			if ((flags & MONO_FIELD_ATTR_PUBLIC) == 0)
				continue;

			m_Fields.emplace(propertyName, iter);
		}
	}

	////////////////////////////////////////////////////////////////////////
	// Script Instance /////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////
	ScriptInstance::ScriptInstance(ScriptInstance* scriptInstance, UUID entityID)
	{
		m_ScriptClass = scriptInstance->m_ScriptClass;

		MonoObject* copy = mono_object_clone(GCManager::GetReferencedObject(scriptInstance->m_Handle));
		m_Handle = GCManager::CreateObjectReference(copy, false);

		ScriptClass entityClass = ScriptClass(s_Data->EntityClass);
		m_Constructor = entityClass.GetMethod(".ctor(ulong)");
		void* params = &entityID;
		entityClass.InvokeMethod(m_Handle, m_Constructor, &params);

		m_OnCreateMethod = m_ScriptClass->GetMethod("OnCreate()");
		m_OnUpdateMethod = m_ScriptClass->GetMethod("OnUpdate(single)");
		m_OnDestroyMethod = m_ScriptClass->GetMethod("OnDestroy()");

		LoadFields();
	}

	ScriptInstance::ScriptInstance(Ref<ScriptClass> scriptClass, UUID entityID)
		: m_ScriptClass(scriptClass)
	{
		m_Handle = scriptClass->Instantiate();

		MonoClass* entityKlass = s_Data->EntityClass;
		ScriptClass entityClass = ScriptClass(entityKlass);
		m_Constructor = entityClass.GetMethod(".ctor(ulong)");
		void* params = &entityID;
		entityClass.InvokeMethod(m_Handle, m_Constructor, &params);

		m_OnCreateMethod = scriptClass->GetMethod("OnCreate()");
		m_OnUpdateMethod = scriptClass->GetMethod("OnUpdate(single)");
		m_OnDestroyMethod = scriptClass->GetMethod("OnDestroy()");

		LoadFields();
	}

	ScriptInstance::~ScriptInstance()
	{
		GCManager::ReleaseObjectReference(m_Handle);
	}

	void ScriptInstance::Invalidate(Ref<ScriptClass> scriptClass, UUID entityID)
	{
		m_ScriptClass = scriptClass;
		m_Handle = scriptClass->Instantiate();

		MonoClass* entityKlass = s_Data->EntityClass;
		ScriptClass entityClass = ScriptClass(entityKlass);
		m_Constructor = entityClass.GetMethod(".ctor(ulong)");
		void* params = &entityID;
		entityClass.InvokeMethod(m_Handle, m_Constructor, &params);

		m_OnCreateMethod = scriptClass->GetMethod("OnCreate()");
		m_OnUpdateMethod = scriptClass->GetMethod("OnUpdate(single)");
		m_OnDestroyMethod = scriptClass->GetMethod("OnDestroy()");

		LoadFields();
	}

	void ScriptInstance::LoadFields()
	{
		auto& fields = m_ScriptClass->m_Fields;
		eastl::map<eastl::string, Ref<Field>> finalFields;

		for (auto [fieldName, monoField] : fields)
		{
			MonoCustomAttrInfo* attr = mono_custom_attrs_from_field(m_ScriptClass->m_MonoClass, monoField);
			MonoType* fieldType = mono_field_get_type(monoField);
			Field::FieldType type = Field::GetFieldType(fieldType);

			bool alreadyPresent = m_Fields.find(fieldName) != m_Fields.end();
			bool sameType = alreadyPresent && m_Fields.at(fieldName)->Type == type;

			if (sameType)
			{
				void* value = m_Fields[fieldName]->GetUnmanagedValue();
				size_t size = m_Fields[fieldName]->GetSize();
				void* copy = new char[size];
				memcpy(copy, value, size);
				finalFields[fieldName] = CreateRef<Field>(fieldName, type, monoField, m_Handle);
				finalFields[fieldName]->SetValue(copy);
				delete[size] copy;
			}
			else
			{
				finalFields[fieldName] = CreateRef<Field>(fieldName, type, monoField, m_Handle);
			}
		}

		m_Fields = finalFields;
	}

	void ScriptInstance::InvokeOnCreate()
	{
		if (m_OnCreateMethod)
			m_ScriptClass->InvokeMethod(m_Handle, m_OnCreateMethod);
	}

	void ScriptInstance::InvokeOnUpdate(float ts)
	{
		if (m_OnUpdateMethod)
		{
			void* params = &ts;
			m_ScriptClass->InvokeMethod(m_Handle, m_OnUpdateMethod, &params);
		}
	}

	void ScriptInstance::InvokeOnDestroy()
	{
		if (m_OnDestroyMethod)
			m_ScriptClass->InvokeMethod(m_Handle, m_OnDestroyMethod);
	}
}
