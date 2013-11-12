/*
 *      Copyright (C) 2005-2012 Team XBMC
 *      http://xbmc.org
 
 *      Copyright (C) 2012 Palle Ehmsen(Barcode Madness)
 *      http://www.barcodemadness.com
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "DVBLinkClient.h"
#include "xbmc/util/StdString.h"

using namespace dvblinkremote;
using namespace dvblinkremotehttp;
using namespace ADDON;

std::string DVBLinkClient::GetBuildInRecorderObjectID()
{
  std::string result = "";
  DVBLinkRemoteStatusCode status;
  GetPlaybackObjectRequest getPlaybackObjectRequest(m_hostname.c_str(), "");
  getPlaybackObjectRequest.RequestedObjectType = GetPlaybackObjectRequest::REQUESTED_OBJECT_TYPE_ALL;
  getPlaybackObjectRequest.RequestedItemType = GetPlaybackObjectRequest::REQUESTED_ITEM_TYPE_ALL;
  getPlaybackObjectRequest.IncludeChildrenObjectsForRequestedObject = true;
  GetPlaybackObjectResponse getPlaybackObjectResponse;
  if ((status = m_dvblinkRemoteCommunication->GetPlaybackObject(getPlaybackObjectRequest, getPlaybackObjectResponse)) == DVBLINK_REMOTE_STATUS_OK)
  {
    for (std::vector<PlaybackContainer*>::iterator it = getPlaybackObjectResponse.GetPlaybackContainers().begin(); it < getPlaybackObjectResponse.GetPlaybackContainers().end(); it++)
    {
      PlaybackContainer * container = (PlaybackContainer *) *it;
      if (strcmp(container->SourceID.c_str(), DVBLINK_BUILD_IN_RECORDER_SOURCE_ID) == 0)
      {
        result = container->GetObjectID();
        break;
      }

    }
  }
  return result;
}



DVBLinkClient::DVBLinkClient(CHelper_libXBMC_addon  *XBMC, CHelper_libXBMC_pvr *PVR,std::string clientname, std::string hostname, long port,bool showinfomsg, std::string username, std::string password, bool usetimeshift, std::string timeshiftpath)
{
  this->PVR = PVR;
  this->XBMC = XBMC;
  m_clientname = clientname;
  m_hostname = hostname;
  m_connected = false;
  m_currentChannelId = 0;
  m_showinfomsg = showinfomsg;

  m_usetimeshift = usetimeshift && XBMC->DirectoryExists(timeshiftpath.c_str());

  m_timeshiftpath.append(timeshiftpath);
  m_timeshiftpath.append(clientname);

  m_httpClient = new HttpPostClient(XBMC,hostname,port, username, password);
  m_dvblinkRemoteCommunication = DVBLinkRemote::Connect((HttpClient&)*m_httpClient, m_hostname.c_str(), 8080, username.c_str(), password.c_str());

  DVBLinkRemoteStatusCode status;
  m_timerCount = -1;
  m_recordingCount = -1;

  GetChannelsRequest request;
  m_channels = new ChannelList();
  m_stream = new Stream();
  m_tsBuffer = NULL;

  if ((status = m_dvblinkRemoteCommunication->GetChannels(request, *m_channels)) == DVBLINK_REMOTE_STATUS_OK)
  {
    int iChannelUnique = 0;
    for (std::vector<Channel*>::iterator it = m_channels->begin(); it < m_channels->end(); it++) 
    {
      Channel* channel = (*it);
      m_channelMap[++iChannelUnique] = channel;
    }
    m_connected = true;
    
    XBMC->Log(LOG_INFO, "Connected to DVBLink Server '%s'",  m_hostname.c_str());
    if (m_showinfomsg)
    {
      XBMC->QueueNotification(QUEUE_INFO, XBMC->GetLocalizedString(32001), m_hostname.c_str());
      XBMC->QueueNotification(QUEUE_INFO, XBMC->GetLocalizedString(32002), m_channelMap.size());
    }

    m_recordingsid = GetBuildInRecorderObjectID();
    m_recordingsid.append(DVBLINK_RECODINGS_BY_DATA_ID);

    m_updating = true;
    CreateThread();
  }
  else
  {
    XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(32003), m_hostname.c_str(), (int)status);
    std::string error;
    m_dvblinkRemoteCommunication->GetLastError(error);
    XBMC->Log(LOG_ERROR, "Could not connect to DVBLink Server '%s' on port '%i' with username '%s' (Error code : %d Description : %s)", hostname.c_str(), port, username.c_str(), (int)status,error.c_str());
  }
}

void *DVBLinkClient::Process()
{
  XBMC->Log(LOG_DEBUG, "DVBLinkUpdateProcess:: thread started");
  unsigned int counter = 0;
  while (m_updating)
  {
    if (counter >= 300000)
    {
      counter = 0;
      PVR->TriggerTimerUpdate();
      Sleep(5000);
      PVR->TriggerRecordingUpdate();
    }
    counter += 1000;
    Sleep(1000);
  }
  XBMC->Log(LOG_DEBUG, "DVBLinkUpdateProcess:: thread stopped");
  return NULL;
}


bool DVBLinkClient::GetStatus()
{
  return m_connected;
}

int DVBLinkClient::GetChannelsAmount()
{
  return m_channelMap.size();
}

PVR_ERROR DVBLinkClient::GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  XBMC->Log(LOG_INFO, "Getting channels (%d channels on server)", m_channelMap.size());
  for (std::map<int,Channel*>::iterator it=m_channelMap.begin(); it!=m_channelMap.end(); ++it)
  {
    Channel* channel = (*it).second;
    
    bool isRadio = (channel->GetChannelType() == Channel::CHANNEL_TYPE_RADIO);

    if (isRadio == bRadio)
    {
      PVR_CHANNEL xbmcChannel;
      memset(&xbmcChannel, 0, sizeof(PVR_CHANNEL));
      xbmcChannel.bIsRadio = isRadio;
      xbmcChannel.iChannelNumber =channel->Number;
      xbmcChannel.iEncryptionSystem = 0;
      xbmcChannel.iUniqueId = (*it).first;

      PVR_STRCPY(xbmcChannel.strChannelName,channel->GetName().c_str());
      CStdString stream;
      if (!m_usetimeshift)
      {
        if (isRadio)
          stream.Format("pvr://stream/radio/%i.ts", channel->GetDvbLinkID());
        else
          stream.Format("pvr://stream/tv/%i.ts", channel->GetDvbLinkID());
        
        PVR_STRCPY(xbmcChannel.strStreamURL, stream.c_str());
        PVR_STRCPY(xbmcChannel.strInputFormat, "video/x-mpegts");
      }

      //PVR_STRCPY(xbmcChannel.strIconPath, "special://userdata/addon_data/pvr.dvblink/channel.png");
      PVR->TransferChannelEntry(handle, &xbmcChannel);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

int DVBLinkClient::GetTimersAmount()
{
  return m_timerCount;
}


int DVBLinkClient::GetInternalUniqueIdFromChannelId(const std::string& channelId)
{
  for (std::map<int,Channel*>::iterator it=m_channelMap.begin(); it!=m_channelMap.end(); ++it)
  {
    Channel * channel = (*it).second;
    int id = (*it).first;
    if (channelId.compare(channel->GetID()) == 0)
    {
      return id;
    }
  }
  return 0;
}

PVR_ERROR DVBLinkClient::GetTimers(ADDON_HANDLE handle)
{
  PVR_ERROR result = PVR_ERROR_FAILED;
  PLATFORM::CLockObject critsec(m_mutex);

  GetSchedulesRequest getSchedulesRequest;
  StoredSchedules sschedules;

  DVBLinkRemoteStatusCode status;
  int count = 0;
  if ((status = m_dvblinkRemoteCommunication->GetSchedules(getSchedulesRequest, sschedules)) != DVBLINK_REMOTE_STATUS_OK)
  {
    std::string error;
    m_dvblinkRemoteCommunication->GetLastError(error);
    XBMC->Log(LOG_ERROR,  "Could not get timers (Error code : %d Description : %s)", (int)status,error.c_str());
   // XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(32006), (int)status);
    return result;
  }

  XBMC->Log(LOG_INFO, "Found %d epg timers", sschedules.GetEpgSchedules().size());
  
  if (m_showinfomsg)
  {
    XBMC->QueueNotification(QUEUE_INFO, XBMC->GetLocalizedString(32007), sschedules.GetEpgSchedules().size());
  }

  for (std::vector<StoredEpgSchedule*>::iterator it = sschedules.GetEpgSchedules().begin(); it < sschedules.GetEpgSchedules().end(); it++) 
  {
    StoredEpgSchedule* schedule = (StoredEpgSchedule*)*it;
    PVR_TIMER xbmcTimer;
    memset(&xbmcTimer, 0, sizeof(PVR_TIMER));
    PVR_STR2INT(xbmcTimer.iClientIndex, schedule->GetID().c_str());
      
    xbmcTimer.iClientChannelUid = GetInternalUniqueIdFromChannelId(schedule->GetChannelID());
    xbmcTimer.state = PVR_TIMER_STATE_SCHEDULED;
    xbmcTimer.bIsRepeating = schedule->Repeat;
    PVR_STR2INT(xbmcTimer.iEpgUid, schedule->GetProgramID().c_str());
    EpgSearchResult epgSearchResult;
    if (DoEPGSearch(epgSearchResult,schedule->GetChannelID(), -1, -1, schedule->GetProgramID()))
    {
      if (epgSearchResult.size() < 1 || epgSearchResult[0]->GetEpgData().size() < 1)
      {
        XBMC->Log(LOG_INFO, "No EPG program data for timer '%s' on channel '%s' with program id '%s'", schedule->GetID().c_str(),schedule->GetChannelID().c_str(),schedule->GetProgramID().c_str());
        continue;
      }
      ChannelEpgData * channelepgdata = epgSearchResult[0];
      Program * program = channelepgdata->GetEpgData()[0];
    
      xbmcTimer.startTime =program->GetStartTime();
      xbmcTimer.endTime = program->GetStartTime() + program->GetDuration();
      PVR_STRCPY(xbmcTimer.strTitle, program->GetTitle().c_str());
      PVR_STRCPY(xbmcTimer.strSummary, program->ShortDescription.c_str());
      PVR->TransferTimerEntry(handle, &xbmcTimer);
      XBMC->Log(LOG_INFO, "Added EPG timer : %s", program->GetTitle().c_str());
      
      count++;
    }      

  }

  XBMC->Log(LOG_INFO, "Found %d manual timers", sschedules.GetManualSchedules().size());

  if (m_showinfomsg)
  {
    XBMC->QueueNotification(QUEUE_INFO, XBMC->GetLocalizedString(32008), sschedules.GetManualSchedules().size());
  }

  for (std::vector<StoredManualSchedule*>::iterator it = sschedules.GetManualSchedules().begin(); it < sschedules.GetManualSchedules().end(); it++) 
  {
    StoredManualSchedule* schedule = (StoredManualSchedule*)*it;
    PVR_TIMER xbmcTimer;
    memset(&xbmcTimer, 0, sizeof(PVR_TIMER));
    PVR_STR2INT(xbmcTimer.iClientIndex, schedule->GetID().c_str());
      
    xbmcTimer.iClientChannelUid = GetInternalUniqueIdFromChannelId(schedule->GetChannelID());


    xbmcTimer.state = PVR_TIMER_STATE_SCHEDULED;
    xbmcTimer.startTime = schedule->GetStartTime();
    xbmcTimer.endTime =  schedule->GetStartTime() + schedule->GetDuration();
    PVR_STRCPY(xbmcTimer.strTitle, schedule->Title.c_str());
    //TODO: PAE: Add weekdays
    XBMC->Log(LOG_INFO, "Added manual timer : %s", schedule->Title.c_str());
    PVR->TransferTimerEntry(handle, &xbmcTimer);
    count++;
  }
  m_timerCount = count;
  result = PVR_ERROR_NO_ERROR;
  return result;
}

PVR_ERROR DVBLinkClient::AddTimer(const PVR_TIMER &timer)
{
  PVR_ERROR result = PVR_ERROR_FAILED;
  PLATFORM::CLockObject critsec(m_mutex);
  DVBLinkRemoteStatusCode status;
  AddScheduleRequest * addScheduleRequest = NULL;
  std::string channelId = m_channelMap[timer.iClientChannelUid]->GetID();
  if (timer.iEpgUid != 0)
  {
    char programId [33];
    PVR_INT2STR(programId,timer.iEpgUid);
    addScheduleRequest = new AddScheduleByEpgRequest(channelId, programId, timer.bIsRepeating);
  }
  else
  {
    //TODO: Fix day mask
    addScheduleRequest = new AddManualScheduleRequest(channelId, timer.startTime, timer.endTime - timer.startTime, -1, timer.strTitle);
  }
  
  if ((status = m_dvblinkRemoteCommunication->AddSchedule(*addScheduleRequest)) == DVBLINK_REMOTE_STATUS_OK)
  {
    XBMC->Log(LOG_INFO, "Timer added");
    PVR->TriggerTimerUpdate();
    result = PVR_ERROR_NO_ERROR;
  }
  else
  {
    result = PVR_ERROR_FAILED;
    std::string error;
    m_dvblinkRemoteCommunication->GetLastError(error);
    XBMC->Log(LOG_ERROR, "Could not add timer (Error code : %d Description : %s)", (int)status, error.c_str());
  }
  SAFE_DELETE(addScheduleRequest);
  return result;
}

PVR_ERROR DVBLinkClient::DeleteTimer(const PVR_TIMER &timer)
{
  PVR_ERROR result = PVR_ERROR_FAILED;
  PLATFORM::CLockObject critsec(m_mutex);
  DVBLinkRemoteStatusCode status;
  char scheduleId [33];
  PVR_INT2STR(scheduleId, timer.iClientIndex);
  
  RemoveScheduleRequest removeSchedule(scheduleId);


  if ((status = m_dvblinkRemoteCommunication->RemoveSchedule(removeSchedule)) == DVBLINK_REMOTE_STATUS_OK)
  {
    XBMC->Log(LOG_INFO, "Timer deleted");
    PVR->TriggerTimerUpdate();
    result = PVR_ERROR_NO_ERROR;
  }
  else
  {
    std::string error;
    m_dvblinkRemoteCommunication->GetLastError(error);
    XBMC->Log(LOG_ERROR, "Timer could not be deleted (Error code : %d Description : %s)", (int)status, error.c_str());
  }
  return result;
}

PVR_ERROR DVBLinkClient::UpdateTimer(const PVR_TIMER &timer)
{
  PVR_ERROR deleteResult = DeleteTimer(timer);
  if (deleteResult == PVR_ERROR_NO_ERROR)
  {
    return AddTimer(timer);
  }
  return deleteResult;
}

int DVBLinkClient::GetRecordingsAmount()
{

  return m_recordingCount;
}



std::string DVBLinkClient::GetRecordedTVByDateObjectID(const std::string& buildInRecoderObjectID)
{
  std::string result = "";
  DVBLinkRemoteStatusCode status;

  GetPlaybackObjectRequest getPlaybackObjectRequest(m_hostname.c_str(), buildInRecoderObjectID);
  getPlaybackObjectRequest.IncludeChildrenObjectsForRequestedObject = true;
  GetPlaybackObjectResponse getPlaybackObjectResponse;


  if ((status = m_dvblinkRemoteCommunication->GetPlaybackObject(getPlaybackObjectRequest, getPlaybackObjectResponse)) == DVBLINK_REMOTE_STATUS_OK)
  {
    for (std::vector<PlaybackContainer*>::iterator it = getPlaybackObjectResponse.GetPlaybackContainers().begin(); it < getPlaybackObjectResponse.GetPlaybackContainers().end(); it++)
    {
      PlaybackContainer * container = (PlaybackContainer *) *it;
      
     
     // if (strcmp(container->GetName().c_str(), "By Date") == 0)
      if (container->GetObjectID().find("F6F08949-2A07-4074-9E9D-423D877270BB") != std::string::npos)
      {
        result = container->GetObjectID();
        break;
      }
    }
  }
  return result;

}

PVR_ERROR DVBLinkClient::DeleteRecording(const PVR_RECORDING& recording)
{
    PLATFORM::CLockObject critsec(m_mutex);
  PVR_ERROR result = PVR_ERROR_FAILED;
  DVBLinkRemoteStatusCode status;
  RemovePlaybackObjectRequest remoteObj(recording.strRecordingId);
  
  if ((status = m_dvblinkRemoteCommunication->RemovePlaybackObject(remoteObj)) != DVBLINK_REMOTE_STATUS_OK)
{
    std::string error;
    m_dvblinkRemoteCommunication->GetLastError(error);
    XBMC->Log(LOG_ERROR, "Recording %s could not be deleted (Error code: %d Description : %s)", recording.strTitle, (int)status, error.c_str());
    return result;
  }

  XBMC->Log(LOG_INFO, "Recording %s deleted", recording.strTitle);
  PVR->TriggerRecordingUpdate();
  result = PVR_ERROR_NO_ERROR;
  return result;
}


PVR_ERROR DVBLinkClient::GetRecordings(ADDON_HANDLE handle)
{
  PLATFORM::CLockObject critsec(m_mutex);
  PVR_ERROR result = PVR_ERROR_FAILED;
  DVBLinkRemoteStatusCode status;

  GetPlaybackObjectRequest getPlaybackObjectRequest(m_hostname.c_str(), m_recordingsid);
  getPlaybackObjectRequest.IncludeChildrenObjectsForRequestedObject = true;
  GetPlaybackObjectResponse getPlaybackObjectResponse;

  if ((status = m_dvblinkRemoteCommunication->GetPlaybackObject(getPlaybackObjectRequest, getPlaybackObjectResponse)) != DVBLINK_REMOTE_STATUS_OK)
  {
    std::string error;
    m_dvblinkRemoteCommunication->GetLastError(error);
    XBMC->Log(LOG_ERROR,"Could not get recordings (Error code : %d Description : %s)", (int)status, error.c_str());
    //XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(32004), (int)status);
    return result;
  }
  
  XBMC->Log(LOG_INFO, "Found %d recordings", getPlaybackObjectResponse.GetPlaybackItems().size());
  
  if (m_showinfomsg)
  {
    XBMC->QueueNotification(QUEUE_INFO, XBMC->GetLocalizedString(32009), getPlaybackObjectResponse.GetPlaybackItems().size());
  }

  for (std::vector<PlaybackItem*>::iterator it = getPlaybackObjectResponse.GetPlaybackItems().begin(); it < getPlaybackObjectResponse.GetPlaybackItems().end(); it++)
  {    
    RecordedTvItem * tvitem = (RecordedTvItem *) *it;
    PVR_RECORDING xbmcRecording;
    memset(&xbmcRecording, 0, sizeof(PVR_RECORDING));
      
    PVR_STRCPY(xbmcRecording.strRecordingId,tvitem->GetObjectID().c_str());
      
    PVR_STRCPY(xbmcRecording.strTitle,tvitem->GetMetadata().GetTitle().c_str());
      
    xbmcRecording.recordingTime = tvitem->GetMetadata().GetStartTime();
    PVR_STRCPY(xbmcRecording.strPlot, tvitem->GetMetadata().ShortDescription.c_str());
    PVR_STRCPY(xbmcRecording.strStreamURL, tvitem->GetPlaybackUrl().c_str());
    xbmcRecording.iDuration =  tvitem->GetMetadata().GetDuration();
    PVR_STRCPY(xbmcRecording.strChannelName, tvitem->ChannelName.c_str());
    PVR_STRCPY(xbmcRecording.strThumbnailPath, tvitem->GetThumbnailUrl().c_str());
    PVR->TransferRecordingEntry(handle, &xbmcRecording);

  }
  m_recordingCount = getPlaybackObjectResponse.GetPlaybackItems().size();
  result = PVR_ERROR_NO_ERROR;
  return result;
}

void DVBLinkClient::GetDriveSpace(long long *iTotal, long long *iUsed)
{
  PLATFORM::CLockObject critsec(m_mutex);
  GetRecordingSettingsRequest recordingsettingsrequest;
  *iTotal = 0;
  *iUsed = 0;
  RecordingSettings settings;
  DVBLinkRemoteStatusCode status;
  if ((status = m_dvblinkRemoteCommunication->GetRecordingSettings(recordingsettingsrequest, settings)) == DVBLINK_REMOTE_STATUS_OK)
  {
    *iTotal = settings.TotalSpace;
    *iUsed = settings.AvailableSpace;
  }
}


int DVBLinkClient::GetCurrentChannelId()
{
  return m_currentChannelId;
}

const char * DVBLinkClient::GetLiveStreamURL(const PVR_CHANNEL &channel, DVBLINK_STREAMTYPE streamtype, int width, int height, int bitrate, std::string audiotrack)
{
  PLATFORM::CLockObject critsec(m_mutex);
  StreamRequest* streamRequest = NULL;
  TranscodingOptions options(width, height);
  options.SetBitrate(bitrate);
  options.SetAudioTrack(audiotrack);
  Channel * c = m_channelMap[channel.iUniqueId];
  DVBLinkRemoteStatusCode status;
  switch (streamtype)
  {
  case HTTP:
    streamRequest = new RawHttpStreamRequest(m_hostname.c_str(), c->GetDvbLinkID(), m_clientname.c_str());
    break;
  case RTP:
    streamRequest = new RealTimeTransportProtocolStreamRequest(m_hostname.c_str(), c->GetDvbLinkID(), m_clientname.c_str(), options);
    break;
  case HLS:
    streamRequest = new HttpLiveStreamRequest(m_hostname.c_str(), c->GetDvbLinkID(), m_clientname.c_str(), options);
    break;
  case ASF:
    streamRequest = new WindowsMediaStreamRequest(m_hostname.c_str(), c->GetDvbLinkID(), m_clientname.c_str(), options);
    break;
  }

  if ((status = m_dvblinkRemoteCommunication->PlayChannel(*streamRequest, *m_stream)) != DVBLINK_REMOTE_STATUS_OK)
  {
    std::string error;
    m_dvblinkRemoteCommunication->GetLastError(error);
    XBMC->Log(LOG_ERROR, "Could not get stream for channel %i (Error code : %d)", channel.iUniqueId, (int)status,error.c_str());
    XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(32010), channel.strChannelName, (int)status);
    SAFE_DELETE(streamRequest);
    return "";
  }

  m_currentChannelId = channel.iUniqueId;
  SAFE_DELETE(streamRequest);
  return m_stream->GetUrl().c_str();
}

bool DVBLinkClient::OpenLiveStream(const PVR_CHANNEL &channel, DVBLINK_STREAMTYPE streamtype, int width, int height, int bitrate, std::string audiotrack)
{
  if (m_usetimeshift && streamtype == HTTP)
  {
    if (m_tsBuffer)
    {
      SAFE_DELETE(m_tsBuffer);
    }
    m_tsBuffer = new TimeShiftBuffer(XBMC, GetLiveStreamURL(channel, streamtype, width, height, bitrate, audiotrack), m_timeshiftpath);
    return m_tsBuffer->IsValid();
  }
  return false;
}

int DVBLinkClient::ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  if(m_tsBuffer)
    return m_tsBuffer->ReadData(pBuffer,iBufferSize);
  return 0;
}

long long DVBLinkClient::SeekLiveStream(long long iPosition, int iWhence)
{
  if(m_tsBuffer)
    return m_tsBuffer->Seek(iPosition, iWhence);
  return 0;
}

long long DVBLinkClient::PositionLiveStream(void)
{
  if(m_tsBuffer)
    return m_tsBuffer->Position();
  return 0;
}
long long DVBLinkClient::LengthLiveStream(void)
{
  if(m_tsBuffer)
    return m_tsBuffer->Length();
  return 0;
}

void DVBLinkClient::StopStreaming(bool bUseChlHandle)
{
  PLATFORM::CLockObject critsec(m_mutex);
  StopStreamRequest * request;
 

  if (m_usetimeshift && m_tsBuffer)
  {
     SAFE_DELETE(m_tsBuffer);
  }

  if (bUseChlHandle)
  {
    request = new StopStreamRequest(m_stream->GetChannelHandle());
  }
  else
  {
    request = new StopStreamRequest(m_clientname);
  }

  DVBLinkRemoteStatusCode status;
  if ((status = m_dvblinkRemoteCommunication->StopChannel(*request)) != DVBLINK_REMOTE_STATUS_OK)
  {
    std::string error;
    m_dvblinkRemoteCommunication->GetLastError(error);
    XBMC->Log(LOG_ERROR, "Could not stop stream (Error code : %d Description : %s)", (int)status, error.c_str());
  }


  SAFE_DELETE(request);
}

void DVBLinkClient::SetEPGGenre(Program *program, EPG_TAG *tag)
{
  if (program->IsCatNews)
  {
    tag->iGenreType = 0x20;
    tag->iGenreSubType = 0x00;
  }

  if (program->IsCatDocumentary)
  {
    tag->iGenreType = 0x20;
    tag->iGenreSubType = 0x03;
  }


  if (program->IsCatEducational)
  {
    tag->iGenreType = 0x90;
  }

  if (program->IsCatSports)
  {
    tag->iGenreType = 0x40;
  }




  if (program->IsCatMovie)
  {
    tag->iGenreType = 0x10;
    tag->iGenreSubType =program->IsCatThriller ? 0x01 : program->IsCatScifi ? 0x03 :program->IsCatHorror ? 0x03 : program->IsCatComedy ? 0x04 : program->IsCatSoap ? 0x05 : program->IsCatRomance ? 0x06 :program->IsCatDrama ? 0x08 : 0;
  }

  if (program->IsCatKids)
  {
    tag->iGenreType = 0x50;
  }

  if (program->IsCatMusic)
  {
    tag->iGenreType = 0x60;
  }

  if (program->IsCatSpecial)
  {
    tag->iGenreType = 0xB0;
  }
}

bool  DVBLinkClient::DoEPGSearch(EpgSearchResult& epgSearchResult, const std::string& channelId, const long startTime, const long endTime, const std::string& programId)
{
  PLATFORM::CLockObject critsec(m_mutex);
  EpgSearchRequest epgSearchRequest(channelId, startTime, endTime);
  if (programId.compare("") != 0)
  {
    epgSearchRequest.ProgramID = programId;
  }

  DVBLinkRemoteStatusCode status;

  if ((status = m_dvblinkRemoteCommunication->SearchEpg(epgSearchRequest, epgSearchResult)) == DVBLINK_REMOTE_STATUS_OK)
  {
    return true;
  }
  return false;
}

PVR_ERROR DVBLinkClient::GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL& channel, time_t iStart, time_t iEnd)
{
  PVR_ERROR result = PVR_ERROR_FAILED;
  PLATFORM::CLockObject critsec(m_mutex);
  Channel * c = m_channelMap[channel.iUniqueId];
  EpgSearchResult epgSearchResult;

  if (DoEPGSearch(epgSearchResult,c->GetID(), iStart, iEnd))
  {
    for (std::vector<ChannelEpgData*>::iterator it = epgSearchResult.begin(); it < epgSearchResult.end(); it++) 
    {
      ChannelEpgData* channelEpgData = (ChannelEpgData*)*it;
      EpgData& epgData = channelEpgData->GetEpgData();
      for (std::vector<Program*>::iterator pIt = epgData.begin(); pIt < epgData.end(); pIt++) 
      {
        Program* p = (Program*)*pIt;
        EPG_TAG broadcast;
        memset(&broadcast, 0, sizeof(EPG_TAG));

        PVR_STR2INT(broadcast.iUniqueBroadcastId, p->GetID().c_str() );
        broadcast.strTitle = p->GetTitle().c_str();
        broadcast.iChannelNumber      = channel.iChannelNumber;
        broadcast.startTime           = p->GetStartTime();
        broadcast.endTime             = p->GetStartTime() + p->GetDuration();
        broadcast.strPlotOutline      = p->SubTitle.c_str();
        broadcast.strPlot             = p->ShortDescription.c_str();
        
        broadcast.strIconPath         = p->Image.c_str();
        broadcast.iGenreType          = 0;
        broadcast.iGenreSubType       = 0;
        broadcast.strGenreDescription = "";
        broadcast.firstAired          = 0;
        broadcast.iParentalRating     = 0;
        broadcast.iStarRating         = p->Rating;
        broadcast.bNotify             = false;
        broadcast.iSeriesNumber       = 0;
        broadcast.iEpisodeNumber      = p->EpisodeNumber;
        broadcast.iEpisodePartNumber  = 0;
        broadcast.strEpisodeName      = "";
        SetEPGGenre(p, &broadcast);
        PVR->TransferEpgEntry(handle, &broadcast);
      }
    }
    result = PVR_ERROR_NO_ERROR;
  }
  else
  {
    XBMC->Log(LOG_NOTICE, "Not EPG data found for channel : %s with id : %i", channel.strChannelName, channel.iUniqueId);
  }
  return result;
}

DVBLinkClient::~DVBLinkClient(void)
{
  m_updating = false;
  if (IsRunning())
  {
    StopThread();
  }
  
  SAFE_DELETE(m_dvblinkRemoteCommunication);
  SAFE_DELETE(m_httpClient);
  SAFE_DELETE(m_channels);
  SAFE_DELETE(m_stream);
  if (m_tsBuffer)
  {
    SAFE_DELETE(m_tsBuffer);
  }
}
