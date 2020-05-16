/*
    FlashX multiplayer script - SEEK mode with flags – 05/16/2020

    Description:
    ------------
    Discover all hidden spots on a map. Spots are marked by flags, when you capture any of these flags, you will get a point. It is a peace mode, you can play only for the VC side.
    Originally made for Die's map Cave_Jump.

    How to use:
    -----------
    *    Place up to 6 standard MP helpers for RW flag on a map
    *    Use standard VC CTF recovery points
    *    The only supported end rule is the time, but use also the point end rule, in case you do not want to set any time limit

    Copyright (c) 2020 FlashX

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use,
    copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following
    conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
    OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
    OTHER DEALINGS IN THE SOFTWARE.
*/

#include <inc\sc_global.h>
#include <inc\sc_def.h>

#define RECOVER_TIME 5.0f // Time to recover player after he was killed
#define NORECOV_TIME 3.0f // Disable time of recoverplace after recovering someone there
#define FLAG_CAPTURED_SHOW_TIME 5.0f
#define FLAG_CAPTURED_ALL_SHOW_TIME 15.0f
#define FLAG_CAPTURE_INFO_SHOW_TIME 7.0f
#define FLAG_MUSIC_TIME 12.0f

#define REC_WPNAME "VCSpawn%d"
#define REC_MAX 64

#define FLAGS_MAX 6 // To preserve the compatibility with the standard RW MP helpers

#define GVAR_FLAG_PHASE 500
#define GVAR_FLAG_REQUEST 501
#define GVAR_FLAG_REQUEST_RESPONSE 502
#define GVAR_FLAG_PL_ADDPOINTS 503
#define GVAR_FLAG_PL_HANDLE 504
#define GVAR_FLAG_PL_FLAG_ID 505
#define GVAR_FLAG_PL_FLAGS_CAPTURED 506

#define FLAG_PH_NONE 0
#define FLAG_PH_CAPT 1

dword gFlagInfo[FLAGS_MAX] = {FALSE, FALSE, FALSE, FALSE, FALSE, FALSE};
ushort wtxt[64], wtxt2[64], wtxt3[64], wtxt4[64];

dword gRecs = 0;
s_SC_MP_Recover gRec[REC_MAX];
float gRecTimer[REC_MAX];

dword gEndRule;
dword gEndValue;
float gTime;

void* gFlagNod[FLAGS_MAX][3];
c_Vector3 gOrigFlagPos[FLAGS_MAX];

dword gFlags = 0;
dword gAnim_pl_id;
BOOL gFirstTick = TRUE;

dword gRequest = 0;
dword gConnectedPlayers = 0;
dword gSendFlagPhase = FLAG_PH_NONE;
float gInfo_txt_timer = 0.0f;

dword gCLN_Request = 0;
dword gCLN_FlagPhase = FLAG_PH_NONE;
dword gCLN_AddPoints = 0;
dword gCLN_LastFlag;
dword gCLN_FlagsCaptured = 0;
BOOL gCLN_FirstTick = TRUE;
float gCLN_info_txt_timer = 0.0f;
float gCLN_music_timer = 0.0f;

BOOL SRV_CheckEndRule(float time)
{
    switch (gEndRule)
    {
    case SC_MP_ENDRULE_TIME:
    {
        if (gConnectedPlayers > 0)
        {
            gTime += time;
        }

        SC_MP_EndRule_SetTimeLeft(gTime, gConnectedPlayers > 0);

        if (gTime > gEndValue)
        {
            SC_MP_LoadNextMap();
            return TRUE;
        }

        break;
    }
    }

    return FALSE;
}

void ChangeFlagState(dword flag, BOOL state)
{
    dword i;

    gCLN_FlagsCaptured = 0;
    gFlagInfo[flag] = state;

    if (state)
    {
        SC_DUMMY_Set_DoNotRenHier2(gFlagNod[flag][0], TRUE);
        SC_DUMMY_Set_DoNotRenHier2(gFlagNod[flag][1], FALSE);
        SC_DUMMY_Set_DoNotRenHier2(gFlagNod[flag][2], TRUE);
    }
    else
    {
        SC_DUMMY_Set_DoNotRenHier2(gFlagNod[flag][0], TRUE);
        SC_DUMMY_Set_DoNotRenHier2(gFlagNod[flag][1], TRUE);
        SC_DUMMY_Set_DoNotRenHier2(gFlagNod[flag][2], FALSE);
    }

    for (i = 0; i < gFlags; i++)
    {
        if (gFlagInfo[i])
        {
            gCLN_FlagsCaptured++;
        }
    }

    swprintf(wtxt, SC_AnsiToUni("Secret rooms discovered: %d/%d", wtxt4), gCLN_FlagsCaptured, gFlags);
}

void DisableAllFlags()
{
    dword i;

    for (i = 0; i < gFlags; i++)
    {
        ChangeFlagState(i, FALSE);
    }
}

int ScriptMain(s_SC_NET_info* info)
{
    char txt[128];
    dword i, pl_id;
    s_SC_MP_Recover* precov;
    s_SC_MP_hud hudinfo;
    s_SC_MP_EnumPlayers enum_pl[64];
    void* nod;
    c_Vector3 vec;
    s_SC_HUD_MP_icon icon[FLAGS_MAX];
    s_SC_MP_SRV_settings SRVset;
    float res_x, res_y;
    dword requestID;

    switch (info->message)
    {
    case SC_NET_MES_SERVER_TICK:
    {
        gConnectedPlayers = 64;
        SC_MP_EnumPlayers(enum_pl, &gConnectedPlayers, SC_MP_ENUMPLAYER_SIDE_ALL);

        if (SRV_CheckEndRule(info->elapsed_time))
        {
            break;
        }

        if (gFirstTick)
        {
            SC_sgi(GVAR_FLAG_REQUEST, 0);
            SC_sgi(GVAR_FLAG_REQUEST_RESPONSE, 0);
            SC_sgi(GVAR_FLAG_PHASE, FLAG_PH_NONE);
            gFirstTick = FALSE;
        }

        // Request ID has changed
        if (gRequest != SC_ggi(GVAR_FLAG_REQUEST))
        {
            gRequest = SC_ggi(GVAR_FLAG_REQUEST);

            pl_id = SC_MP_GetPlofHandle(SC_ggi(GVAR_FLAG_PL_HANDLE));
            SC_P_MP_AddPoints(pl_id, SC_ggi(GVAR_FLAG_PL_ADDPOINTS));

            gInfo_txt_timer = SC_ggi(GVAR_FLAG_PL_FLAGS_CAPTURED) == gFlags ? FLAG_CAPTURED_ALL_SHOW_TIME : FLAG_CAPTURED_SHOW_TIME;
            gSendFlagPhase = FLAG_PH_CAPT;

            // Inform clients about the successful execution of the last request
            SC_sgi(GVAR_FLAG_REQUEST_RESPONSE, gRequest);
        }

        if (gInfo_txt_timer > 0.0f)
        {
            gInfo_txt_timer -= info->elapsed_time;
        }
        else
        {
            gSendFlagPhase = FLAG_PH_NONE;
        }

        if (gSendFlagPhase != SC_ggi(GVAR_FLAG_PHASE))
        {
            SC_sgi(GVAR_FLAG_PHASE, gSendFlagPhase);
        }

        break;
    }

    case SC_NET_MES_CLIENT_TICK:
    {
        if (gCLN_FirstTick)
        {
            gCLN_FlagPhase = SC_ggi(GVAR_FLAG_PHASE);
            gCLN_Request = SC_ggi(GVAR_FLAG_REQUEST);
            gCLN_FirstTick = FALSE;
        }

        if (gCLN_FlagPhase != SC_ggi(GVAR_FLAG_PHASE))
        {
            gCLN_FlagPhase = SC_ggi(GVAR_FLAG_PHASE);

            if (gCLN_FlagPhase == FLAG_PH_CAPT)
            {
                i = SC_ggi(GVAR_FLAG_PL_FLAGS_CAPTURED);

                if (i == gFlags)
                {
                    pl_id = SC_MP_GetPlofHandle(SC_ggi(GVAR_FLAG_PL_HANDLE));

                    SC_SND_MusicPlay(pl_id == SC_PC_Get() ? 12 : 45, 100);

                    if (SC_P_GetActive(pl_id))
                    {
                        SC_P_DoAnimLooped(pl_id, "g\\characters\\anims\\a901 menu vc.stg");
                        gAnim_pl_id = pl_id;
                    }

                    gCLN_music_timer = FLAG_MUSIC_TIME;
                }
            }
        }

        if (gCLN_Request != SC_ggi(GVAR_FLAG_REQUEST))
        {
            gCLN_Request = SC_ggi(GVAR_FLAG_REQUEST);
        }

        if (gCLN_music_timer > 0.0f)
        {
            gCLN_music_timer -= info->elapsed_time;

            if (gCLN_music_timer <= 0.0f)
            {
                SC_SND_MusicStopFade(45, 4000);

                if (SC_P_GetActive(gAnim_pl_id))
                {
                    SC_P_DoAnim(gAnim_pl_id, NULL);
                }
            }
        }

        if (SC_P_GetActive(SC_PC_Get()))
        {
            SC_PC_GetPos(&vec);

            for (i = 0; i < gFlags; i++)
            {
                if (!gFlagInfo[i])
                {
                    if (SC_IsNear3D(&gOrigFlagPos[i], &vec, 1.5f))
                    {
                        ChangeFlagState(i, TRUE);
                        gCLN_LastFlag = i;
                        gCLN_AddPoints++;

                        gCLN_info_txt_timer = FLAG_CAPTURE_INFO_SHOW_TIME; // Reset the timer

                        if (gCLN_FlagsCaptured == gFlags)
                        {
                            swprintf(wtxt2, SC_AnsiToUni("Congrats, you've discovered all the secret rooms", wtxt4));
                        }
                        else
                        {
                            swprintf(wtxt2, SC_AnsiToUni("You've discovered the secret room %d", wtxt4), i + 1);
                        }

                        if (gCLN_FlagsCaptured != gFlags)
                        {
                            SC_SND_PlaySound2D(11116);
                        }
                    }
                }
            }
        }

        if (gCLN_AddPoints > 0 && SC_ggi(GVAR_FLAG_REQUEST) == SC_ggi(GVAR_FLAG_REQUEST_RESPONSE)) // Check whether anyone else is not currently requesting a point addition
        {
            // Pass the info about player to the server
            SC_sgi(GVAR_FLAG_PL_ADDPOINTS, gCLN_AddPoints);
            SC_sgi(GVAR_FLAG_PL_HANDLE, SC_MP_GetHandleofPl(SC_PC_Get()));
            SC_sgi(GVAR_FLAG_PL_FLAG_ID, gCLN_LastFlag);
            SC_sgi(GVAR_FLAG_PL_FLAGS_CAPTURED, gCLN_FlagsCaptured);

            requestID = rand();
            if (requestID == gCLN_Request) // This situation could theoretically occur
            {
                requestID++;
            }

            SC_sgi(GVAR_FLAG_REQUEST, requestID); // Send the request
            gCLN_AddPoints = 0;
        }

        for (i = 0; i < gFlags; i++)
        {
            icon[i].color = 0xffffffff;
            icon[i].type = SC_HUD_MP_ICON_TYPE_NONE;

            if (gFlagInfo[i])
            {
                icon[i].icon_id = 3; // VC flag icon
            }
            else
            {
                icon[i].icon_id = 12; // Neutral flag icon
            }
        }

        if (SC_KeyPressed(0x0F) || gCLN_info_txt_timer > 0.0f) // Show flag icons when the TAB key is pressed
        {
            SC_MP_SetIconHUD(icon, gFlags);
        }
        else
        {
            SC_MP_SetIconHUD(icon, 0);
        }

        if (gCLN_info_txt_timer > 0.0f)
        {
            gCLN_info_txt_timer -= info->elapsed_time;
        }

        break;
    }

    case SC_NET_MES_LEVELPREINIT:
    {
        SC_sgi(GVAR_MP_MISSIONTYPE, GVAR_MP_MISSIONTYPE_RW);
        SC_MP_Gvar_SetSynchro(GVAR_FLAG_PHASE);
        SC_MP_Gvar_SetSynchro(GVAR_FLAG_REQUEST);
        SC_MP_Gvar_SetSynchro(GVAR_FLAG_REQUEST_RESPONSE);
        SC_MP_Gvar_SetSynchro(GVAR_FLAG_PL_HANDLE);
        SC_MP_Gvar_SetSynchro(GVAR_FLAG_PL_ADDPOINTS);
        SC_MP_Gvar_SetSynchro(GVAR_FLAG_PL_FLAG_ID);
        SC_MP_Gvar_SetSynchro(GVAR_FLAG_PL_FLAGS_CAPTURED);

        gEndRule = info->param1;
        gEndValue = info->param2;
        gTime = 0.0f;

        SC_MP_EnableBotsFromScene(FALSE);

        break;
    }

    case SC_NET_MES_LEVELINIT:
    {
        SC_MP_SRV_SetForceSide(SC_P_SIDE_VC);
        SC_MP_SetChooseValidSides(1);

        SC_MP_SRV_SetClassLimit(18, 0);
        SC_MP_SRV_SetClassLimit(19, 0);
        SC_MP_SRV_SetClassLimit(39, 0);

        SC_MP_GetSRVsettings(&SRVset);

        for (i = 0; i < 6; i++)
        {
            SC_MP_SRV_SetClassLimit(i + 1, SRVset.atg_class_limit[i]);
            SC_MP_SRV_SetClassLimit(i + 21, SRVset.atg_class_limit[i]);
        }

        CLEAR(hudinfo);
        hudinfo.title = 1071;

        hudinfo.sort_by[0] = SC_HUD_MP_SORTBY_POINTS;
        hudinfo.sort_by[1] = SC_HUD_MP_SORTBY_DEATHS | SC_HUD_MP_SORT_DOWNUP;
        hudinfo.sort_by[2] = SC_HUD_MP_SORTBY_PINGS | SC_HUD_MP_SORT_DOWNUP;

        hudinfo.pl_mask = SC_HUD_MP_PL_MASK_CLASS | SC_HUD_MP_PL_MASK_POINTS | SC_HUD_MP_PL_MASK_DEATHS;
        hudinfo.use_sides = TRUE;
        hudinfo.side_name[0] = 1010;
        hudinfo.side_color[0] = 0x440000ff;
        hudinfo.side_name[1] = 1011;
        hudinfo.side_color[1] = 0x44ff0000;
        hudinfo.disableUSside = TRUE;

        hudinfo.side_mask = SC_HUD_MP_SIDE_MASK_FRAGS;

        SC_MP_HUD_SetTabInfo(&hudinfo);

        SC_MP_AllowStPwD(TRUE);
        SC_MP_AllowFriendlyFireOFF(TRUE);

        SC_MP_SetItemsNoDisappear(FALSE);

        CLEAR(icon);
        CLEAR(gFlagInfo);

        if (info->param2)
        {
            SC_MP_SetInstantRecovery(TRUE);

            gFlags = 0;

            // Get all flag nodes
            for (i = 0; i < FLAGS_MAX; i++)
            {
                sprintf(txt, "W%c_flag", 'A' + i);
                nod = SC_NOD_GetNoMessage(NULL, txt);

                if (nod)
                {
                    gFlagNod[gFlags][0] = SC_NOD_Get(nod, "vlajkaUS");
                    gFlagNod[gFlags][1] = SC_NOD_Get(nod, "Vlajka VC");
                    gFlagNod[gFlags][2] = SC_NOD_Get(nod, "vlajka N");

                    if (!gFlagNod[gFlags][0])
                    {
                        SC_message("US flag not found: %s", txt);
                    }
                    if (!gFlagNod[gFlags][1])
                    {
                        SC_message("VC flag not found: %s", txt);
                    }
                    if (!gFlagNod[gFlags][2])
                    {
                        SC_message("Neutral flag not found: %s", txt);
                    }

                    SC_NOD_GetWorldPos(nod, &gOrigFlagPos[gFlags]);

                    gFlags++;
                }
            }

            DisableAllFlags();

            if (info->param1) // It's a server
            {
                gRecs = 0;

                for (i = 0; i < REC_MAX; i++)
                {
                    sprintf(txt, REC_WPNAME, i);
                    if (SC_NET_FillRecover(&gRec[gRecs], txt))
                    {
                        gRecs++;
                    }
                }

#if _GE_VERSION_ >= 133
                i = REC_MAX - gRecs;
                SC_MP_GetRecovers(SC_MP_RESPAWN_CTF_VC, &gRec[gRecs], &i);
                gRecs += i;
#endif

                if (gRecs == 0)
                {
                    SC_message("No SEEK recover place defined!");
                }

                CLEAR(gRecTimer);
            }
        }

        break;
    }

    case SC_NET_MES_RENDERHUD:
    {
        if (SC_KeyPressed(0x0F) || gCLN_info_txt_timer > 0.0f) // Show HUD info when the TAB key is pressed
        {
            SC_GetScreenRes(&res_x, &res_y);

            res_x -= SC_Fnt_GetWidthW(wtxt, 1) + 15;
            res_y = 15;

            SC_Fnt_WriteW(res_x, res_y, wtxt, 1, gCLN_FlagsCaptured == gFlags ? 0xffceba36 : 0xffffffff);
        }

        if (gCLN_info_txt_timer > (gCLN_FlagsCaptured == gFlags ? 0.0f : (FLAG_CAPTURE_INFO_SHOW_TIME - FLAG_CAPTURED_SHOW_TIME)) && !SC_KeyPressed(0x0F)) // Show info text when PC has captured a flag
        {
            SC_GetScreenRes(&res_x, &res_y);

            res_x -= SC_Fnt_GetWidthW(wtxt2, 1);

            SC_Fnt_WriteW(res_x * 0.5f, res_y * 0.5f, wtxt2, 1, gCLN_FlagsCaptured == gFlags ? 0xffceba36 : 0xffffffff);
        }
        else // Show info text when anyone else has captured a flag
        {
            if (gCLN_FlagPhase == FLAG_PH_CAPT)
            {
                pl_id = SC_MP_GetPlofHandle(SC_ggi(GVAR_FLAG_PL_HANDLE));
                if (pl_id && pl_id != SC_PC_Get())
                {
                    i = SC_ggi(GVAR_FLAG_PL_FLAGS_CAPTURED);

                    if (i == gFlags)
                    {
                        swprintf(wtxt3, SC_AnsiToUni("%S has discovered all the secret rooms", wtxt4), SC_P_GetName(pl_id));
                    }
                    else
                    {
                        swprintf(wtxt3, SC_AnsiToUni("%S has just discovered the secret room %d", wtxt4), SC_P_GetName(pl_id), SC_ggi(GVAR_FLAG_PL_FLAG_ID) + 1);
                    }

                    SC_GetScreenRes(&res_x, &res_y);

                    res_x -= SC_Fnt_GetWidthW(wtxt3, 1);

                    SC_Fnt_WriteW(res_x * 0.5f, 15, wtxt3, 1, i == gFlags ? 0xffceba36 : 0xffffffff);
                }
            }
        }

        break;
    }

    case SC_NET_MES_SERVER_RECOVER_TIME:
    {
        if (info->param2)
        {
            info->fval1 = 0.1f;
        }
        else // Killed
        {
            info->fval1 = RECOVER_TIME;
        }

        break;
    }

    case SC_NET_MES_SERVER_RECOVER_PLACE:
    {
        precov = (s_SC_MP_Recover*)info->param2;

        i = SC_MP_SRV_GetBestDMrecov(gRec, gRecs, gRecTimer, NORECOV_TIME);

        gRecTimer[i] = NORECOV_TIME;
        *precov = gRec[i];

        break;
    }

    case SC_NET_MES_RESTARTMAP:
    {
        gTime = 0.0f;

        SC_MP_SRV_ClearPlsStats();

        break;
    }

    case SC_NET_MES_RULESCHANGED:
    {
        gEndRule = info->param1;
        gEndValue = info->param2;
        gTime = 0.0f;

        break;
    }
    }

    return 1;
}
