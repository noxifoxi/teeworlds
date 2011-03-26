/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>

#include <game/generated/protocol.h>
#include <base/vmath.h>
#include <game/client/render.h>
#include "voting.h"

void CVoting::ConCallvote(IConsole::IResult *pResult, void *pUserData)
{
	CVoting *pSelf = (CVoting*)pUserData;
	pSelf->Callvote(pResult->GetString(0), pResult->GetString(1), pResult->NumArguments() > 2 ? pResult->GetString(2) : "");
}

void CVoting::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CVoting *pSelf = (CVoting *)pUserData;
	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->Vote(1);
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->Vote(-1);
}

void CVoting::Callvote(const char *pType, const char *pValue, const char *pReason)
{
	CNetMsg_Cl_CallVote Msg = {0};
	Msg.m_Type = pType;
	Msg.m_Value = pValue;
	Msg.m_Reason = pReason;
	Client()->SendPackMsg(&Msg, MSGFLAG_VITAL);
}

void CVoting::CallvoteSpectate(int ClientID, const char *pReason, bool ForceVote)
{
	if(ForceVote)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "set_team %d -1", ClientID);
		Client()->Rcon(aBuf);
	}
	else
	{
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "%d", ClientID);
		Callvote("spectate", aBuf, pReason);
	}
}

void CVoting::CallvoteKick(int ClientID, const char *pReason, bool ForceVote)
{
	if(ForceVote)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "force_vote kick %d %s", ClientID, pReason);
		Client()->Rcon(aBuf);
	}
	else
	{
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "%d", ClientID);
		Callvote("kick", aBuf, pReason);
	}
}

void CVoting::CallvoteOption(int OptionId, const char *pReason, bool ForceVote)
{
	CVoteOptionClient *pOption = m_pFirst;
	while(pOption && OptionId >= 0)
	{
		if(OptionId == 0)
		{
			if(ForceVote)
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "force_vote option \"%s\" %s", pOption->m_aDescription, pReason);
				Client()->Rcon(aBuf);
			}
			else
				Callvote("option", pOption->m_aDescription, pReason);
			break;
		}
		
		OptionId--;
		pOption = pOption->m_pNext;
	}
}

void CVoting::Vote(int v)
{
	CNetMsg_Cl_Vote Msg = {v};
	Client()->SendPackMsg(&Msg, MSGFLAG_VITAL);
}

CVoting::CVoting()
{
	ClearOptions();
	OnReset();
}


void CVoting::ClearOptions()
{
	m_Heap.Reset();

	m_NumVoteOptions = 0;	
	m_pFirst = 0;
	m_pLast = 0;

	m_pRecycleFirst = 0;
	m_pRecycleLast = 0;
}

void CVoting::OnReset()
{
	m_Closetime = 0;
	m_aDescription[0] = 0;
	m_aReason[0] = 0;
	m_Yes = m_No = m_Pass = m_Total = 0;
	m_Voted = 0;
}

void CVoting::OnConsoleInit()
{
	Console()->Register("callvote", "ss?r", CFGFLAG_CLIENT, ConCallvote, this, "Call vote");
	Console()->Register("vote", "r", CFGFLAG_CLIENT, ConVote, this, "Vote yes/no");
}

void CVoting::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType == NETMSGTYPE_SV_VOTESET)
	{
		CNetMsg_Sv_VoteSet *pMsg = (CNetMsg_Sv_VoteSet *)pRawMsg;
		if(pMsg->m_Timeout)
		{
			OnReset();
			str_copy(m_aDescription, pMsg->m_pDescription, sizeof(m_aDescription));
			str_copy(m_aReason, pMsg->m_pReason, sizeof(m_aReason));
			m_Closetime = time_get() + time_freq() * pMsg->m_Timeout;
		}
		else
			OnReset();
	}
	else if(MsgType == NETMSGTYPE_SV_VOTESTATUS)
	{
		CNetMsg_Sv_VoteStatus *pMsg = (CNetMsg_Sv_VoteStatus *)pRawMsg;
		m_Yes = pMsg->m_Yes;
		m_No = pMsg->m_No;
		m_Pass = pMsg->m_Pass;
		m_Total = pMsg->m_Total;
	}	
	else if(MsgType == NETMSGTYPE_SV_VOTECLEAROPTIONS)
	{
		ClearOptions();
	}
	else if(MsgType == NETMSGTYPE_SV_VOTEOPTIONADD)
	{
		CNetMsg_Sv_VoteOptionAdd *pMsg = (CNetMsg_Sv_VoteOptionAdd *)pRawMsg;
	
		CVoteOptionClient *pOption;
		if(m_pRecycleFirst)
		{
			pOption = m_pRecycleFirst;
			m_pRecycleFirst = m_pRecycleFirst->m_pNext;
			if(m_pRecycleFirst)
				m_pRecycleFirst->m_pPrev = 0;
			else
				m_pRecycleLast = 0;
		}
		else
			pOption = (CVoteOptionClient *)m_Heap.Allocate(sizeof(CVoteOptionClient));

		pOption->m_pNext = 0;
		pOption->m_pPrev = m_pLast;
		if(pOption->m_pPrev)
			pOption->m_pPrev->m_pNext = pOption;
		m_pLast = pOption;
		if(!m_pFirst)
			m_pFirst = pOption;
		
		str_copy(pOption->m_aDescription, pMsg->m_pDescription, sizeof(pOption->m_aDescription));
		++m_NumVoteOptions;
	}
	else if(MsgType == NETMSGTYPE_SV_VOTEOPTIONREMOVE)
	{
		CNetMsg_Sv_VoteOptionRemove *pMsg = (CNetMsg_Sv_VoteOptionRemove *)pRawMsg;
	
		for(CVoteOptionClient *pOption = m_pFirst; pOption; pOption = pOption->m_pNext)
		{
			if(str_comp(pOption->m_aDescription, pMsg->m_pDescription) == 0)
			{
				// remove it from the list
				if(m_pFirst == pOption)
					m_pFirst = m_pFirst->m_pNext;
				if(m_pLast == pOption)
					m_pLast = m_pLast->m_pPrev;
				if(pOption->m_pPrev)
					pOption->m_pPrev->m_pNext = pOption->m_pNext;
				if(pOption->m_pNext)
					pOption->m_pNext->m_pPrev = pOption->m_pPrev;
				--m_NumVoteOptions;

				// add it to recycle list
				pOption->m_pNext = 0;
				pOption->m_pPrev = m_pRecycleLast;
				if(pOption->m_pPrev)
					pOption->m_pPrev->m_pNext = pOption;
				m_pRecycleLast = pOption;
				if(!m_pRecycleFirst)
					m_pRecycleLast = pOption;

				break;
			}
		}
	}
}

void CVoting::OnRender()
{
}


void CVoting::RenderBars(CUIRect Bars, bool Text)
{
	RenderTools()->DrawUIRect(&Bars, vec4(0.8f,0.8f,0.8f,0.5f), CUI::CORNER_ALL, Bars.h/3);
	
	CUIRect Splitter = Bars;
	Splitter.x = Splitter.x+Splitter.w/2;
	Splitter.w = Splitter.h/2.0f;
	Splitter.x -= Splitter.w/2;
	RenderTools()->DrawUIRect(&Splitter, vec4(0.4f,0.4f,0.4f,0.5f), CUI::CORNER_ALL, Splitter.h/4);
			
	if(m_Total)
	{
		CUIRect PassArea = Bars;
		if(m_Yes)
		{
			CUIRect YesArea = Bars;
			YesArea.w *= m_Yes/(float)m_Total;
			RenderTools()->DrawUIRect(&YesArea, vec4(0.2f,0.9f,0.2f,0.85f), CUI::CORNER_ALL, Bars.h/3);
			
			if(Text)
			{
				char Buf[256];
				str_format(Buf, sizeof(Buf), "%d", m_Yes);
				UI()->DoLabel(&YesArea, Buf, Bars.h*0.75f, 0);
			}
			
			PassArea.x += YesArea.w;
			PassArea.w -= YesArea.w;
		}
		
		if(m_No)
		{
			CUIRect NoArea = Bars;
			NoArea.w *= m_No/(float)m_Total;
			NoArea.x = (Bars.x + Bars.w)-NoArea.w;
			RenderTools()->DrawUIRect(&NoArea, vec4(0.9f,0.2f,0.2f,0.85f), CUI::CORNER_ALL, Bars.h/3);
			
			if(Text)
			{
				char Buf[256];
				str_format(Buf, sizeof(Buf), "%d", m_No);
				UI()->DoLabel(&NoArea, Buf, Bars.h*0.75f, 0);
			}

			PassArea.w -= NoArea.w;
		}

		if(Text && m_Pass)
		{
			char Buf[256];
			str_format(Buf, sizeof(Buf), "%d", m_Pass);
			UI()->DoLabel(&PassArea, Buf, Bars.h*0.75f, 0);
		}
	}	
}


