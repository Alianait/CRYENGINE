// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved.

#include "stdafx.h"
#include "Impl.h"
#include "CVars.h"
#include "Common.h"
#include "Event.h"
#include "EventInstance.h"
#include "Listener.h"
#include "Object.h"
#include "VolumeParameter.h"
#include "VolumeState.h"

#include <IEnvironmentConnection.h>
#include <IFile.h>
#include <ISettingConnection.h>
#include <FileInfo.h>
#include <CrySystem/File/CryFile.h>
#include <CryAudio/IAudioSystem.h>
#include <CrySystem/IProjectManager.h>
#include <CrySystem/IConsole.h>
#include <CrySystem/ConsoleRegistration.h>

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	#include <Logger.h>
	#include <DebugStyle.h>
	#include <CrySystem/ITimer.h>
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE

#include <SDL_mixer.h>

namespace CryAudio
{
namespace Impl
{
namespace SDL_mixer
{
SPoolSizes g_poolSizes;
std::map<ContextId, SPoolSizes> g_contextPoolSizes;

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
SPoolSizes g_debugPoolSizes;
uint16 g_objectPoolSize = 0;
uint16 g_eventInstancePoolSize = 0;

EventInstances g_constructedEventInstances;
std::vector<CListener*> g_constructedListeners;
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE

//////////////////////////////////////////////////////////////////////////
void CountPoolSizes(XmlNodeRef const& node, SPoolSizes& poolSizes)
{
	uint16 numEvents = 0;
	node->getAttr(g_szEventsAttribute, numEvents);
	poolSizes.events += numEvents;

	uint16 numParameters = 0;
	node->getAttr(g_szParametersAttribute, numParameters);
	poolSizes.parameters += numParameters;

	uint16 numSwitchStates = 0;
	node->getAttr(g_szSwitchStatesAttribute, numSwitchStates);
	poolSizes.switchStates += numSwitchStates;
}

//////////////////////////////////////////////////////////////////////////
void AllocateMemoryPools(uint16 const objectPoolSize, uint16 const eventPoolSize)
{
	CObject::CreateAllocator(objectPoolSize);
	CEventInstance::CreateAllocator(eventPoolSize);
	CEvent::CreateAllocator(g_poolSizes.events);
	CVolumeParameter::CreateAllocator(g_poolSizes.parameters);
	CVolumeState::CreateAllocator(g_poolSizes.switchStates);
}

//////////////////////////////////////////////////////////////////////////
void FreeMemoryPools()
{
	CObject::FreeMemoryPool();
	CEventInstance::FreeMemoryPool();
	CEvent::FreeMemoryPool();
	CVolumeParameter::FreeMemoryPool();
	CVolumeState::FreeMemoryPool();
}

//////////////////////////////////////////////////////////////////////////
string GetFullFilePath(char const* const szFileName, char const* const szPath)
{
	string fullFilePath;

	if ((szPath != nullptr) && (szPath[0] != '\0'))
	{
		fullFilePath = szPath;
		fullFilePath += "/";
		fullFilePath += szFileName;
	}
	else
	{
		fullFilePath = szFileName;
	}

	return fullFilePath;
}

///////////////////////////////////////////////////////////////////////////
CImpl::CImpl()
#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	: m_name("SDL Mixer 2.0.2")
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE
{
	g_pImpl = this;

#if CRY_PLATFORM_WINDOWS
	m_memoryAlignment = 16;
#elif CRY_PLATFORM_MAC
	m_memoryAlignment = 16;
#elif CRY_PLATFORM_LINUX
	m_memoryAlignment = 16;
#elif CRY_PLATFORM_IOS
	m_memoryAlignment = 16;
#elif CRY_PLATFORM_ANDROID
	m_memoryAlignment = 16;
#else
	#error "Undefined platform."
#endif
}

///////////////////////////////////////////////////////////////////////////
void CImpl::Update()
{
	SoundEngine::Update();
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::Init(uint16 const objectPoolSize)
{
	if (g_cvars.m_eventPoolSize < 1)
	{
		g_cvars.m_eventPoolSize = 1;

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
		Cry::Audio::Log(ELogType::Warning, R"(Event pool size must be at least 1. Forcing the cvar "s_SDLMixerEventPoolSize" to 1!)");
#endif    // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE
	}

	g_objects.reserve(static_cast<size_t>(objectPoolSize));
	AllocateMemoryPools(objectPoolSize, static_cast<uint16>(g_cvars.m_eventPoolSize));

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	g_constructedEventInstances.reserve(static_cast<size_t>(g_cvars.m_eventPoolSize));
	g_objectPoolSize = objectPoolSize;
	g_eventInstancePoolSize = static_cast<uint16>(g_cvars.m_eventPoolSize);
#endif        // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE

	if (ICVar* const pCVar = gEnv->pConsole->GetCVar("g_languageAudio"))
	{
		SetLanguage(pCVar->GetString());
	}

	if (SoundEngine::Init())
	{
		return ERequestStatus::Success;
	}

	return ERequestStatus::Failure;
}

///////////////////////////////////////////////////////////////////////////
void CImpl::ShutDown()
{
}

///////////////////////////////////////////////////////////////////////////
void CImpl::Release()
{
	SoundEngine::Release();

	delete this;
	g_pImpl = nullptr;
	g_cvars.UnregisterVariables();

	FreeMemoryPools();
}

///////////////////////////////////////////////////////////////////////////
void CImpl::OnRefresh()
{
	SoundEngine::Refresh();
}

//////////////////////////////////////////////////////////////////////////
void CImpl::SetLibraryData(XmlNodeRef const& node, ContextId const contextId)
{
	if (contextId == GlobalContextId)
	{
		CountPoolSizes(node, g_poolSizes);
	}
	else
	{
		CountPoolSizes(node, g_contextPoolSizes[contextId]);
	}
}

//////////////////////////////////////////////////////////////////////////
void CImpl::OnBeforeLibraryDataChanged()
{
	ZeroStruct(g_poolSizes);
	g_contextPoolSizes.clear();

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	ZeroStruct(g_debugPoolSizes);
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE
}

//////////////////////////////////////////////////////////////////////////
void CImpl::OnAfterLibraryDataChanged(int const poolAllocationMode)
{
	if (!g_contextPoolSizes.empty())
	{
		if (poolAllocationMode <= 0)
		{
			for (auto const& poolSizePair : g_contextPoolSizes)
			{
				SPoolSizes const& iterPoolSizes = g_contextPoolSizes[poolSizePair.first];

				g_poolSizes.events += iterPoolSizes.events;
				g_poolSizes.parameters += iterPoolSizes.parameters;
				g_poolSizes.switchStates += iterPoolSizes.switchStates;
			}
		}
		else
		{
			SPoolSizes maxContextPoolSizes;

			for (auto const& poolSizePair : g_contextPoolSizes)
			{
				SPoolSizes const& iterPoolSizes = g_contextPoolSizes[poolSizePair.first];

				maxContextPoolSizes.events = std::max(maxContextPoolSizes.events, iterPoolSizes.events);
				maxContextPoolSizes.parameters = std::max(maxContextPoolSizes.parameters, iterPoolSizes.parameters);
				maxContextPoolSizes.switchStates = std::max(maxContextPoolSizes.switchStates, iterPoolSizes.switchStates);
			}

			g_poolSizes.events += maxContextPoolSizes.events;
			g_poolSizes.parameters += maxContextPoolSizes.parameters;
			g_poolSizes.switchStates += maxContextPoolSizes.switchStates;
		}
	}

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	// Used to hide pools without allocations in debug draw.
	g_debugPoolSizes = g_poolSizes;
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE

	g_poolSizes.events = std::max<uint16>(1, g_poolSizes.events);
	g_poolSizes.parameters = std::max<uint16>(1, g_poolSizes.parameters);
	g_poolSizes.switchStates = std::max<uint16>(1, g_poolSizes.switchStates);
}

///////////////////////////////////////////////////////////////////////////
void CImpl::OnLoseFocus()
{
	SoundEngine::Mute();
}

///////////////////////////////////////////////////////////////////////////
void CImpl::OnGetFocus()
{
	SoundEngine::UnMute();
}

///////////////////////////////////////////////////////////////////////////
void CImpl::MuteAll()
{
	SoundEngine::Mute();
}

///////////////////////////////////////////////////////////////////////////
void CImpl::UnmuteAll()
{
	SoundEngine::UnMute();
}

///////////////////////////////////////////////////////////////////////////
void CImpl::PauseAll()
{
	SoundEngine::Pause();
}

///////////////////////////////////////////////////////////////////////////
void CImpl::ResumeAll()
{
	SoundEngine::Resume();
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::StopAllSounds()
{
	SoundEngine::Stop();
	return ERequestStatus::Success;
}

///////////////////////////////////////////////////////////////////////////
void CImpl::RegisterInMemoryFile(SFileInfo* const pFileInfo)
{
}

///////////////////////////////////////////////////////////////////////////
void CImpl::UnregisterInMemoryFile(SFileInfo* const pFileInfo)
{
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CImpl::ConstructFile(XmlNodeRef const& rootNode, SFileInfo* const pFileInfo)
{
	return ERequestStatus::Failure;
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructFile(IFile* const pIFile)
{
	delete pIFile;
}

///////////////////////////////////////////////////////////////////////////
char const* const CImpl::GetFileLocation(SFileInfo* const pFileInfo)
{
	static CryFixedStringT<MaxFilePathLength> s_path;
	s_path = CRY_AUDIO_DATA_ROOT "/";
	s_path += g_szImplFolderName;
	s_path += "/";
	s_path += g_szAssetsFolderName;
	s_path += "/";

	return s_path.c_str();
}

//////////////////////////////////////////////////////////////////////////
void CImpl::GetInfo(SImplInfo& implInfo) const
{
#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	implInfo.name = m_name.c_str();
#else
	implInfo.name = "name-not-present-in-release-mode";
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE
	implInfo.folderName = g_szImplFolderName;
}

///////////////////////////////////////////////////////////////////////////
ITriggerConnection* CImpl::ConstructTriggerConnection(XmlNodeRef const& rootNode, float& radius)
{
	ITriggerConnection* pITriggerConnection = nullptr;

	if (_stricmp(rootNode->getTag(), g_szEventTag) == 0)
	{
		char const* const szFileName = rootNode->getAttr(g_szNameAttribute);
		char const* const szPath = rootNode->getAttr(g_szPathAttribute);
		string const fullFilePath = GetFullFilePath(szFileName, szPath);

		char const* const szLocalized = rootNode->getAttr(g_szLocalizedAttribute);
		bool const isLocalized = (szLocalized != nullptr) && (_stricmp(szLocalized, g_szTrueValue) == 0);

		SampleId const sampleId = SoundEngine::LoadSample(fullFilePath, true, isLocalized);
		CEvent::EActionType type = CEvent::EActionType::Start;

		if (_stricmp(rootNode->getAttr(g_szTypeAttribute), g_szStopValue) == 0)
		{
			type = CEvent::EActionType::Stop;
		}
		else if (_stricmp(rootNode->getAttr(g_szTypeAttribute), g_szPauseValue) == 0)
		{
			type = CEvent::EActionType::Pause;
		}
		else if (_stricmp(rootNode->getAttr(g_szTypeAttribute), g_szResumeValue) == 0)
		{
			type = CEvent::EActionType::Resume;
		}

		if (type == CEvent::EActionType::Start)
		{
			bool const isPanningEnabled = (_stricmp(rootNode->getAttr(g_szPanningEnabledAttribute), g_szTrueValue) == 0);
			bool const isAttenuationEnabled = (_stricmp(rootNode->getAttr(g_szAttenuationEnabledAttribute), g_szTrueValue) == 0);

			float minDistance = -1.0f;
			float maxDistance = -1.0f;

			if (isAttenuationEnabled)
			{
				rootNode->getAttr(g_szAttenuationMinDistanceAttribute, minDistance);
				rootNode->getAttr(g_szAttenuationMaxDistanceAttribute, maxDistance);

				minDistance = std::max(0.0f, minDistance);
				maxDistance = std::max(0.0f, maxDistance);

				if (minDistance > maxDistance)
				{
#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
					Cry::Audio::Log(ELogType::Warning, "Min distance (%f) was greater than max distance (%f) of %s", minDistance, maxDistance, szFileName);
#endif          // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE

					std::swap(minDistance, maxDistance);
				}

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
				radius = maxDistance;
#endif        // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE
			}

			// Translate decibel to normalized value.
			static const int maxVolume = 128;
			float volume = 0.0f;
			rootNode->getAttr(g_szVolumeAttribute, volume);
			auto const normalizedVolume = static_cast<int>(pow_tpl(10.0f, volume / 20.0f) * maxVolume);

			int numLoops = 0;
			rootNode->getAttr(g_szLoopCountAttribute, numLoops);
			// --numLoops because -1: play infinite, 0: play once, 1: play twice, etc...
			--numLoops;
			// Max to -1 to stay backwards compatible.
			numLoops = std::max(-1, numLoops);

			float fadeInTimeSec = g_defaultFadeInTime;
			rootNode->getAttr(g_szFadeInTimeAttribute, fadeInTimeSec);
			auto const fadeInTimeMs = static_cast<int>(fadeInTimeSec * 1000.0f);

			float fadeOutTimeSec = g_defaultFadeOutTime;
			rootNode->getAttr(g_szFadeOutTimeAttribute, fadeOutTimeSec);
			auto const fadeOutTimeMs = static_cast<int>(fadeOutTimeSec * 1000.0f);

			MEMSTAT_CONTEXT(EMemStatContextType::AudioImpl, "CryAudio::Impl::SDL_mixer::CEvent");

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
			pITriggerConnection = static_cast<ITriggerConnection*>(
				new CEvent(
					fullFilePath.c_str(),
					StringToId(fullFilePath.c_str()),
					type,
					sampleId,
					minDistance,
					maxDistance,
					normalizedVolume,
					numLoops,
					fadeInTimeMs,
					fadeOutTimeMs,
					isPanningEnabled));
#else
			pITriggerConnection = static_cast<ITriggerConnection*>(
				new CEvent(
					StringToId(fullFilePath.c_str()),
					type,
					sampleId,
					minDistance,
					maxDistance,
					normalizedVolume,
					numLoops,
					fadeInTimeMs,
					fadeOutTimeMs,
					isPanningEnabled));
#endif        // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE
		}
		else
		{
			MEMSTAT_CONTEXT(EMemStatContextType::AudioImpl, "CryAudio::Impl::SDL_mixer::CEvent");

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
			pITriggerConnection = static_cast<ITriggerConnection*>(new CEvent(fullFilePath.c_str(), StringToId(fullFilePath.c_str()), type, sampleId));

#else
			pITriggerConnection = static_cast<ITriggerConnection*>(new CEvent(StringToId(fullFilePath.c_str()), type, sampleId));
#endif        // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE
		}
	}
#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	else
	{
		Cry::Audio::Log(ELogType::Warning, "Unknown SDL Mixer tag: %s", rootNode->getTag());
	}
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE

	return pITriggerConnection;
}

//////////////////////////////////////////////////////////////////////////
ITriggerConnection* CImpl::ConstructTriggerConnection(ITriggerInfo const* const pITriggerInfo)
{
#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	ITriggerConnection* pITriggerConnection = nullptr;
	auto const pTriggerInfo = static_cast<STriggerInfo const*>(pITriggerInfo);

	if (pTriggerInfo != nullptr)
	{
		string const fullFilePath = GetFullFilePath(pTriggerInfo->name.c_str(), pTriggerInfo->path.c_str());
		SampleId const sampleId = SoundEngine::LoadSample(fullFilePath, true, pTriggerInfo->isLocalized);

		MEMSTAT_CONTEXT(EMemStatContextType::AudioImpl, "CryAudio::Impl::SDL_mixer::CEvent");
		pITriggerConnection = static_cast<ITriggerConnection*>(
			new CEvent(
				fullFilePath.c_str(),
				StringToId(fullFilePath.c_str()),
				CEvent::EActionType::Start,
				sampleId,
				-1.0f,
				-1.0f,
				128,
				0,
				0,
				0,
				false));
	}

	return pITriggerConnection;
#else
	return nullptr;
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructTriggerConnection(ITriggerConnection const* const pITriggerConnection)
{
	auto const pEvent = static_cast<CEvent const*>(pITriggerConnection);
	pEvent->SetToBeDestructed();

	if (pEvent->CanBeDestructed())
	{
		delete pEvent;
	}
}

///////////////////////////////////////////////////////////////////////////
IParameterConnection* CImpl::ConstructParameterConnection(XmlNodeRef const& rootNode)
{
	IParameterConnection* pIParameterConnection = nullptr;

	if (_stricmp(rootNode->getTag(), g_szEventTag) == 0)
	{
		char const* const szFileName = rootNode->getAttr(g_szNameAttribute);
		char const* const szPath = rootNode->getAttr(g_szPathAttribute);
		string const fullFilePath = GetFullFilePath(szFileName, szPath);

		char const* const szLocalized = rootNode->getAttr(g_szLocalizedAttribute);
		bool const isLocalized = (szLocalized != nullptr) && (_stricmp(szLocalized, g_szTrueValue) == 0);

		SampleId const sampleId = SoundEngine::LoadSample(fullFilePath, true, isLocalized);

		float multiplier = g_defaultParamMultiplier;
		float shift = g_defaultParamShift;
		rootNode->getAttr(g_szMutiplierAttribute, multiplier);
		multiplier = std::max(0.0f, multiplier);
		rootNode->getAttr(g_szShiftAttribute, shift);

		MEMSTAT_CONTEXT(EMemStatContextType::AudioImpl, "CryAudio::Impl::SDL_mixer::CParameter");

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
		pIParameterConnection = static_cast<IParameterConnection*>(new CVolumeParameter(sampleId, multiplier, shift, szFileName));
#else
		pIParameterConnection = static_cast<IParameterConnection*>(new CVolumeParameter(sampleId, multiplier, shift));
#endif    // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE
	}

	return pIParameterConnection;
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructParameterConnection(IParameterConnection const* const pIParameterConnection)
{
	delete pIParameterConnection;
}

///////////////////////////////////////////////////////////////////////////
ISwitchStateConnection* CImpl::ConstructSwitchStateConnection(XmlNodeRef const& rootNode)
{
	ISwitchStateConnection* pISwitchStateConnection = nullptr;

	if (_stricmp(rootNode->getTag(), g_szEventTag) == 0)
	{
		char const* const szFileName = rootNode->getAttr(g_szNameAttribute);
		char const* const szPath = rootNode->getAttr(g_szPathAttribute);
		string const fullFilePath = GetFullFilePath(szFileName, szPath);

		char const* const szLocalized = rootNode->getAttr(g_szLocalizedAttribute);
		bool const isLocalized = (szLocalized != nullptr) && (_stricmp(szLocalized, g_szTrueValue) == 0);

		SampleId const sampleId = SoundEngine::LoadSample(fullFilePath, true, isLocalized);

		float value = g_defaultStateValue;
		rootNode->getAttr(g_szValueAttribute, value);
		value = crymath::clamp(value, 0.0f, 1.0f);

		MEMSTAT_CONTEXT(EMemStatContextType::AudioImpl, "CryAudio::Impl::SDL_mixer::CSwitchState");

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
		pISwitchStateConnection = static_cast<ISwitchStateConnection*>(new CVolumeState(sampleId, value, szFileName));
#else
		pISwitchStateConnection = static_cast<ISwitchStateConnection*>(new CVolumeState(sampleId, value));
#endif    // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE
	}

	return pISwitchStateConnection;
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructSwitchStateConnection(ISwitchStateConnection const* const pISwitchStateConnection)
{
	delete pISwitchStateConnection;
}

///////////////////////////////////////////////////////////////////////////
IEnvironmentConnection* CImpl::ConstructEnvironmentConnection(XmlNodeRef const& rootNode)
{
	return nullptr;
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructEnvironmentConnection(IEnvironmentConnection const* const pIEnvironmentConnection)
{
	delete pIEnvironmentConnection;
}

//////////////////////////////////////////////////////////////////////////
ISettingConnection* CImpl::ConstructSettingConnection(XmlNodeRef const& rootNode)
{
	return nullptr;
}

//////////////////////////////////////////////////////////////////////////
void CImpl::DestructSettingConnection(ISettingConnection const* const pISettingConnection)
{
	delete pISettingConnection;
}

///////////////////////////////////////////////////////////////////////////
IObject* CImpl::ConstructGlobalObject(IListeners const& listeners)
{
	return ConstructObject(CTransformation::GetEmptyObject(), listeners, "Global Object");
}

///////////////////////////////////////////////////////////////////////////
IObject* CImpl::ConstructObject(CTransformation const& transformation, IListeners const& listeners, char const* const szName /*= nullptr*/)
{
	auto const pListener = static_cast<CListener*>(listeners[0]);

	MEMSTAT_CONTEXT(EMemStatContextType::AudioImpl, "CryAudio::Impl::SDL_mixer::CObject");
	auto const pObject = new CObject(transformation, pListener);

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	if (szName != nullptr)
	{
		pObject->SetName(szName);
	}

	if (listeners.size() > 1)
	{
		Cry::Audio::Log(ELogType::Warning, R"(More than one listener is passed in %s. SDL Mixer supports only one listener per object. Listener "%s" will be used for object "%s".)",
		                __FUNCTION__, pListener->GetName(), pObject->GetName());
	}
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE

	g_objects.push_back(pObject);
	return static_cast<IObject*>(pObject);
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructObject(IObject const* const pIObject)
{
	if (pIObject != nullptr)
	{
		auto const pObject = static_cast<CObject const*>(pIObject);
		stl::find_and_erase(g_objects, pObject);
		delete pObject;
	}
}

///////////////////////////////////////////////////////////////////////////
IListener* CImpl::ConstructListener(CTransformation const& transformation, char const* const szName)
{
	MEMSTAT_CONTEXT(EMemStatContextType::AudioImpl, "CryAudio::Impl::SDL_mixer::CListener");
	auto const pListener = new CListener(transformation);

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	pListener->SetName(szName);
	g_constructedListeners.push_back(pListener);
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE

	return static_cast<IListener*>(pListener);
}

///////////////////////////////////////////////////////////////////////////
void CImpl::DestructListener(IListener* const pIListener)
{
	auto const pListener = static_cast<CListener*>(pIListener);

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	auto iter(g_constructedListeners.begin());
	auto const iterEnd(g_constructedListeners.cend());

	for (; iter != iterEnd; ++iter)
	{
		if ((*iter) == pListener)
		{
			if (iter != (iterEnd - 1))
			{
				(*iter) = g_constructedListeners.back();
			}

			g_constructedListeners.pop_back();
			break;
		}
	}
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE

	delete pListener;
}

///////////////////////////////////////////////////////////////////////////
void CImpl::GamepadConnected(DeviceId const deviceUniqueID)
{}

///////////////////////////////////////////////////////////////////////////
void CImpl::GamepadDisconnected(DeviceId const deviceUniqueID)
{}

///////////////////////////////////////////////////////////////////////////
void CImpl::SetLanguage(char const* const szLanguage)
{
	if (szLanguage != nullptr)
	{
		bool const shouldReload = !m_language.IsEmpty() && (m_language.compareNoCase(szLanguage) != 0);

		m_language = szLanguage;
		s_localizedAssetsPath = PathUtil::GetLocalizationFolder().c_str();
		s_localizedAssetsPath += "/";
		s_localizedAssetsPath += m_language.c_str();
		s_localizedAssetsPath += "/";
		s_localizedAssetsPath += CRY_AUDIO_DATA_ROOT;
		s_localizedAssetsPath += "/";
		s_localizedAssetsPath += g_szImplFolderName;
		s_localizedAssetsPath += "/";
		s_localizedAssetsPath += g_szAssetsFolderName;
		s_localizedAssetsPath += "/";

		if (shouldReload)
		{
			SoundEngine::Refresh();
		}
	}
}

#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
//////////////////////////////////////////////////////////////////////////
CEventInstance* CImpl::ConstructEventInstance(TriggerInstanceId const triggerInstanceId, CEvent& event, CObject const& object)
{
	event.IncrementNumInstances();
	MEMSTAT_CONTEXT(EMemStatContextType::AudioImpl, "CryAudio::Impl::SDL_mixer::CEventInstance");

	auto const pEventInstance = new CEventInstance(triggerInstanceId, event, object);
	g_constructedEventInstances.push_back(pEventInstance);

	return pEventInstance;
}
#else
//////////////////////////////////////////////////////////////////////////
CEventInstance* CImpl::ConstructEventInstance(TriggerInstanceId const triggerInstanceId, CEvent& event)
{
	event.IncrementNumInstances();

	MEMSTAT_CONTEXT(EMemStatContextType::AudioImpl, "CryAudio::Impl::SDL_mixer::CEventInstance");
	return new CEventInstance(triggerInstanceId, event);
}
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE

//////////////////////////////////////////////////////////////////////////
void CImpl::DestructEventInstance(CEventInstance const* const pEventInstance)
{
#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	CRY_ASSERT_MESSAGE(pEventInstance != nullptr, "pEventInstance is nullpter during %s", __FUNCTION__);

	auto iter(g_constructedEventInstances.begin());
	auto const iterEnd(g_constructedEventInstances.cend());

	for (; iter != iterEnd; ++iter)
	{
		if ((*iter) == pEventInstance)
		{
			if (iter != (iterEnd - 1))
			{
				(*iter) = g_constructedEventInstances.back();
			}

			g_constructedEventInstances.pop_back();
			break;
		}
	}

#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE

	CEvent* const pEvent = &pEventInstance->GetEvent();
	delete pEventInstance;

	pEvent->DecrementNumInstances();

	if (pEvent->CanBeDestructed())
	{
		delete pEvent;
	}
}

//////////////////////////////////////////////////////////////////////////
void CImpl::DrawDebugMemoryInfo(IRenderAuxGeom& auxGeom, float const posX, float& posY, bool const drawDetailedInfo)
{
#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	float const headerPosY = posY;
	posY += Debug::g_systemHeaderLineSpacerHeight;

	size_t totalPoolSize = 0;

	{
		auto& allocator = CObject::GetAllocator();
		size_t const memAlloc = allocator.GetTotalMemory().nAlloc;
		totalPoolSize += memAlloc;

		if (drawDetailedInfo)
		{
			Debug::DrawMemoryPoolInfo(auxGeom, posX, posY, memAlloc, allocator.GetCounts(), "Objects", g_objectPoolSize);
		}
	}

	{
		auto& allocator = CEventInstance::GetAllocator();
		size_t const memAlloc = allocator.GetTotalMemory().nAlloc;
		totalPoolSize += memAlloc;

		if (drawDetailedInfo)
		{
			Debug::DrawMemoryPoolInfo(auxGeom, posX, posY, memAlloc, allocator.GetCounts(), "Event Instances", g_eventInstancePoolSize);
		}
	}

	if (g_debugPoolSizes.events > 0)
	{
		auto& allocator = CEvent::GetAllocator();
		size_t const memAlloc = allocator.GetTotalMemory().nAlloc;
		totalPoolSize += memAlloc;

		if (drawDetailedInfo)
		{
			Debug::DrawMemoryPoolInfo(auxGeom, posX, posY, memAlloc, allocator.GetCounts(), "Events", g_poolSizes.events);
		}
	}

	if (g_debugPoolSizes.parameters > 0)
	{
		auto& allocator = CVolumeParameter::GetAllocator();
		size_t const memAlloc = allocator.GetTotalMemory().nAlloc;
		totalPoolSize += memAlloc;

		if (drawDetailedInfo)
		{
			Debug::DrawMemoryPoolInfo(auxGeom, posX, posY, memAlloc, allocator.GetCounts(), "Parameters", g_poolSizes.parameters);
		}
	}

	if (g_debugPoolSizes.switchStates > 0)
	{
		auto& allocator = CVolumeState::GetAllocator();
		size_t const memAlloc = allocator.GetTotalMemory().nAlloc;
		totalPoolSize += memAlloc;

		if (drawDetailedInfo)
		{
			Debug::DrawMemoryPoolInfo(auxGeom, posX, posY, memAlloc, allocator.GetCounts(), "SwitchStates", g_poolSizes.switchStates);
		}
	}

	CryModuleMemoryInfo memInfo;
	ZeroStruct(memInfo);
	CryGetMemoryInfoForModule(&memInfo);

	CryFixedStringT<Debug::MaxMemInfoStringLength> memAllocSizeString;
	auto const memAllocSize = static_cast<size_t>(memInfo.allocated - memInfo.freed);
	Debug::FormatMemoryString(memAllocSizeString, memAllocSize - totalPoolSize);

	CryFixedStringT<Debug::MaxMemInfoStringLength> totalPoolSizeString;
	Debug::FormatMemoryString(totalPoolSizeString, totalPoolSize);

	CryFixedStringT<Debug::MaxMemInfoStringLength> totalSamplesString;
	Debug::FormatMemoryString(totalSamplesString, g_loadedSampleSize);

	CryFixedStringT<Debug::MaxMemInfoStringLength> totalMemSizeString;
	size_t const totalMemSize = memAllocSize + g_loadedSampleSize;
	Debug::FormatMemoryString(totalMemSizeString, totalMemSize);

	auxGeom.Draw2dLabel(posX, headerPosY, Debug::g_systemHeaderFontSize, Debug::s_globalColorHeader, false, "%s (System: %s | Pools: %s | Samples: %s | Total: %s)",
	                    m_name.c_str(), memAllocSizeString.c_str(), totalPoolSizeString.c_str(), totalSamplesString.c_str(), totalMemSizeString.c_str());

	size_t const numEvents = g_constructedEventInstances.size();
	size_t const numListeners = g_constructedListeners.size();

	posY += Debug::g_systemLineHeight;
	auxGeom.Draw2dLabel(posX, posY, Debug::g_systemFontSize, Debug::s_systemColorTextSecondary, false, "Active Events: %3" PRISIZE_T " | Listeners: %" PRISIZE_T, numEvents, numListeners);

	for (auto const pListener : g_constructedListeners)
	{
		Vec3 const& listenerPosition = pListener->GetTransformation().GetPosition();
		Vec3 const& listenerDirection = pListener->GetTransformation().GetForward();
		char const* const szName = pListener->GetName();

		posY += Debug::g_systemLineHeight;
		auxGeom.Draw2dLabel(posX, posY, Debug::g_systemFontSize, Debug::s_systemColorListenerActive, false, "Listener: %s | PosXYZ: %.2f %.2f %.2f | FwdXYZ: %.2f %.2f %.2f",
		                    szName, listenerPosition.x, listenerPosition.y, listenerPosition.z, listenerDirection.x, listenerDirection.y, listenerDirection.z);
	}
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE
}

//////////////////////////////////////////////////////////////////////////
void CImpl::DrawDebugInfoList(IRenderAuxGeom& auxGeom, float& posX, float posY, Vec3 const& camPos, float const debugDistance, char const* const szTextFilter) const
{
#if defined(CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE)
	CryFixedStringT<MaxControlNameLength> lowerCaseSearchString(szTextFilter);
	lowerCaseSearchString.MakeLower();

	auxGeom.Draw2dLabel(posX, posY, Debug::g_listHeaderFontSize, Debug::s_globalColorHeader, false, "SDL Mixer Events [%" PRISIZE_T "]", g_constructedEventInstances.size());
	posY += Debug::g_listHeaderLineHeight;

	for (auto const pEventInstance : g_constructedEventInstances)
	{
		Vec3 const& position = pEventInstance->GetObject().GetTransformation().GetPosition();
		float const distance = position.GetDistance(camPos);

		if ((debugDistance <= 0.0f) || ((debugDistance > 0.0f) && (distance < debugDistance)))
		{
			char const* const szEventName = pEventInstance->GetEvent().GetName();
			CryFixedStringT<MaxControlNameLength> lowerCaseEventName(szEventName);
			lowerCaseEventName.MakeLower();
			bool const draw = ((lowerCaseSearchString.empty() || (lowerCaseSearchString == "0")) || (lowerCaseEventName.find(lowerCaseSearchString) != CryFixedStringT<MaxControlNameLength>::npos));

			if (draw)
			{
				ColorF color = Debug::s_listColorItemActive;
				char const* const szListenerName = (pEventInstance->GetObject().GetListener() != nullptr) ? pEventInstance->GetObject().GetListener()->GetName() : "No Listener!";

				CryFixedStringT<MaxMiscStringLength> debugText;
				debugText.Format("%s on %s (%s)", szEventName, pEventInstance->GetObject().GetName(), szListenerName);

				if (pEventInstance->IsFadingOut())
				{
					float const fadeOutTime = static_cast<float>(pEventInstance->GetEvent().GetFadeOutTime()) / 1000.0f;
					float const remainingTime = std::max(0.0f, fadeOutTime - (gEnv->pTimer->GetAsyncTime().GetSeconds() - pEventInstance->GetTimeFadeOutStarted()));

					debugText.Format("%s on %s (%s) (%.2f sec)", szEventName, pEventInstance->GetObject().GetName(), szListenerName, remainingTime);
					color = Debug::s_listColorItemStopping;
				}

				auxGeom.Draw2dLabel(posX, posY, Debug::g_listFontSize, color, false, "%s", debugText.c_str());

				posY += Debug::g_listLineHeight;
			}
		}
	}

	posX += 600.0f;
#endif  // CRY_AUDIO_IMPL_SDLMIXER_USE_DEBUG_CODE
}
} // namespace SDL_mixer
} // namespace Impl
} // namespace CryAudio
