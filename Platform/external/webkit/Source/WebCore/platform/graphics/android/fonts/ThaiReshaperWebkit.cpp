/*
i * Copyright (C) 2006 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ThaiReshaperWebkit.h"
#include <cutils/log.h>

#define  equal(a,b) !((a)^(b))

///////////////////////////////////////////////////////////////////////////////
//[SISO][Saurabh] thai Reshaper - 15-07-11 starts
/**
 * Function to check if uniCode is thai unicode or not
 *//*** checked***/
bool isThai(unsigned short uniCode) {
	if ((uniCode >= KO_KAI) && (uniCode <= KHO_MUT)) {
#ifdef THAIRESHAPERLOGS
		LOGE("ThaiReshaper::isThai, true");
#endif
		return true;
	}
#ifdef THAIRESHAPERLOGS
	LOGE("ThaiReshaper::isThai, false");
#endif
	return false;
}

/*

 * Function to reshape thai character
 **/
///////////////////////////////////////////////////////////////////////////////
//[SISO][Saurabh] thai Reshaper - 15-07-11 starts
/**
 *  Function returns a positive integer or 0 if the c requires certain
 *  tones applied to it to be reshaped. whether tone is required to be reshaped is
 *  determined by using isThaiReshaperTone.
 **//**check***/
int isThaiReshaperCharhw(int32_t c) {

#ifdef THAIRESHAPERLOGS
	LOGE("ThaiReshaper::isThaiReshaperChar, unicode is %x", c);
#endif
	if ((c == YO_YING) || (c == THO_THAN)) {
		return THAIRESHAPEREMOVETAIL;
	} else if ((c == TO_PATAK) || (c == DO_CHADA)) {
		return THAIRESHAPEPULLDOWN;
	} else if ((c == PO_PLA) || (c == FO_FA) || (c == FO_FAN) ||(c == LO_CHULLA)) {
		return THAIRESHAPESHIFTLEFT;
	} else {
		return NOTTHAIRESHAPE;
	}



}

/**
 *  Function to check if reshaping is required on the tone.
 *  it returns integer value corresponding to the integer returned by
 *  isThaiReshaperChar, to check if current tone corresponds to the tones required
 *  to be reshaped for consonant in contention.
 **/
int isThaiReshaperTonehw(int32_t uniCode, int32_t consonantType) {
	if (uniCode == SARA_U || uniCode == SARA_UU || uniCode == PHINTHU) {
		if (consonantType == THAIRESHAPEREMOVETAIL)
			return THAIRESHAPEREMOVETAIL;
		else
			return THAIRESHAPEPULLDOWN;
	} else if (((uniCode >= MAI_EK ) && (uniCode <= NIKHAHIT)) || ((uniCode
			>= SARA_AM) && (uniCode <= SARA_Uee)) || (uniCode == MAI_HAN_AKAT)
			|| (uniCode == MAI_TAI_KHU)) {
		return THAIRESHAPESHIFTLEFT;
	} else {
		return NOTTHAIRESHAPE;
	}

}

/**
 * [SNMC Brower Team] [yogendra Singh] 
 *  Function to replace Thai unicode characters in place in an input string if reshaping is required.
 *  It returns true if atleast once character is replaced.
 **/
bool thaiReshaperInPlace(UChar** originalString, size_t stringLength){
    UChar* string = *originalString;
    int result = NOTTHAIRESHAPE;
    bool replaced = false;	
    for(size_t i=0; i<stringLength; i++)
    {
        if(isThai(string[i])){
            int cT=isThaiReshaperCharhw(string[i]);
            switch(cT){
                case THAIRESHAPEREMOVETAIL: 
                    result=isThaiReshaperTonehw(string[++i],cT);
                    if(equal(result,THAIRESHAPEREMOVETAIL)){
                        if(equal(string[i-1], YO_YING)) {
                            string[i-1]= YO_YING_CUT_TAIL;
                        }
                        else {
                            string[i-1]= THO_THAN_CUT_TAIL;
                        }
                        replaced = true;
                    }
                    else if(equal(result,NOTTHAIRESHAPE)) {
                        i--;
                    }

                    break;
                case THAIRESHAPEPULLDOWN:
                    result=isThaiReshaperTonehw(string[++i],cT);
                    if(equal(result, THAIRESHAPEPULLDOWN)){
                        if (equal(string[i], SARA_U)) {
                            string[i] = SARA_U_DOWN; // SARA_U_PULL_DOWN 
                        } else if (equal(string[i], SARA_UU)) {
                            string[i] = SARA_UU_DOWN; // SARA_UU_PULL_DOWN 
                        } else {
                            string[i] = PHINTHU_DOWN; //PHINTHU_DOWN
                        }
                        replaced = true;
                    }
                    else if(equal(result,NOTTHAIRESHAPE)){
                        i--;
                    }
                    break;
                case THAIRESHAPESHIFTLEFT: 
                    { 
                            bool isThirdCharacterIsThaiTone=!isNotThaiTone(string[i+2]);
                            result=isThaiReshaperTonehw(string[++i],cT);
                            if (equal(result, THAIRESHAPESHIFTLEFT)) {
                                    if (equal(string[i] , MAI_HAN_AKAT)) {
                                            string[i] = MAI_HAN_AKAT_LEFT_SHIFT; // MAI_HAN_AKAT_LEFT_SHIFT 
                                    } else if (equal(string[i] , SARA_I)) {
                                            string[i] = SARA_I_LEFT_SHIFT; // SARA_I_LEFT_SHIFT 
                                    }else if (equal(string[i] , SARA_AM)) {
                                            string[i] = SARA_AM_LEFT_SHIFT; // SARA_AM_LEFT_SHIFT 
                                    } else if (equal(string[i] ,SARA_Ii)) {
                                            string[i] = SARA_Ii_LEFT_SHIFT; // SARA_Ii_LEFT_SHIFT 
                                    } else if (equal(string[i] ,SARA_Ue)) {
                                            string[i] = SARA_Ue_LEFT_SHIFT; // SARA_Ue_LEFT_SHIFT 
                                    } else if (equal(string[i] , SARA_Uee)) {
                                            string[i] = SARA_Uee_LEFT_SHIFT; // SARA_Uee_LEFT_SHIFT 
                                    } else if (equal(string[i] , MAI_TAI_KHU)) {
                                            string[i] = MAI_TAI_KHU_LEFT_SHIFT; // MAI_TAI_KHU_LEFT_SHIFT /
                                    } else if (equal(string[i] , MAI_EK)) {
                                            string[i] = MAI_EK_LEFT_SHIFT; // MAI_EK_LEFT_SHIFT 
                                    } else if (equal(string[i] , MAI_THO)) {
                                            string[i] = MAI_THO_LEFT_SHIFT; // MAI_THO_LEFT_SHIFT 
                                    } else if (equal(string[i] , MAI_TRI)) {
                                            string[i] = MAI_TRI_LEFT_SHIFT; //MAI_TRI_LEFT_SHIFT
                                    } else if (equal(string[i] , MAI_CHATTAWA)) {
                                            string[i] = MAI_CHATTAWA_LEFT_SHIFT; //MAI_CHATTAWA_LEFT_SHIFT
                                    } else if (equal(string[i] , THANTHAKHAT)) {
                                            string[i] = THANTHAKHAT_LEFT_SHIFT; //THANTHAKHAT_LEFT_SHIFT
                                    } else if (equal(string[i] , NIKHAHIT)) {
                                            string[i] = NIKHAHIT_LEFT_SHIFT; //NIKHAHIT_LEFT_SHIFT
                                    } 
                                    replaced = true;
                            }
                            else if(equal(result,NOTTHAIRESHAPE)){
                                    i--;
                            }
                            if(isThirdCharacterIsThaiTone==true){
                                    result=isThaiReshaperTonehw(string[++i],cT);
                                    if (equal(result, THAIRESHAPESHIFTLEFT)) {
                                            if (equal(string[i] , MAI_HAN_AKAT)) {
                                                    string[i] = MAI_HAN_AKAT_LEFT_SHIFT; // MAI_HAN_AKAT_LEFT_SHIFT 
                                            }else if (equal(string[i] , SARA_I)) {
                                                    string[i] = SARA_I_LEFT_SHIFT; // SARA_I_LEFT_SHIFT 
                                            }else if (equal(string[i] , SARA_AM)) {
                                                    string[i] = SARA_AM_LEFT_SHIFT; // SARA_AM_LEFT_SHIFT
                                            } else if (equal(string[i] ,SARA_Ii)) {
                                                    string[i] = SARA_Ii_LEFT_SHIFT; // SARA_Ii_LEFT_SHIFT 
                                            } else if (equal(string[i] ,SARA_Ue)) {
                                                    string[i] = SARA_Ue_LEFT_SHIFT; // SARA_Ue_LEFT_SHIFT 
                                            } else if (equal(string[i] , SARA_Uee)) {
                                                    string[i] = SARA_Uee_LEFT_SHIFT; // SARA_Uee_LEFT_SHIFT 
                                            } else if (equal(string[i] , MAI_TAI_KHU)) {
                                                    string[i] = MAI_TAI_KHU_LEFT_SHIFT; // MAI_TAI_KHU_LEFT_SHIFT /
                                            } else if (equal(string[i] , MAI_EK)) {
                                                    string[i] = MAI_EK_LEFT_SHIFT; // MAI_EK_LEFT_SHIFT 
                                            } else if (equal(string[i] , MAI_THO)) {
                                                    string[i] = MAI_THO_LEFT_SHIFT; // MAI_THO_LEFT_SHIFT 
                                            } else if (equal(string[i] , MAI_TRI)) {
                                                    string[i] = MAI_TRI_LEFT_SHIFT; //MAI_TRI_LEFT_SHIFT
                                            } else if (equal(string[i] , MAI_CHATTAWA)) {
                                                    string[i] = MAI_CHATTAWA_LEFT_SHIFT; //MAI_CHATTAWA_LEFT_SHIFT
                                            } else if (equal(string[i] , THANTHAKHAT)) {
                                                    string[i] = THANTHAKHAT_LEFT_SHIFT; //THANTHAKHAT_LEFT_SHIFT
                                            } else if (equal(string[i] , NIKHAHIT)) {
                                                    string[i] = NIKHAHIT_LEFT_SHIFT; //NIKHAHIT_LEFT_SHIFT
                                            } 
                                            replaced = true;
                                    }
                                    else if(equal(result,NOTTHAIRESHAPE)){
                                            i--;
                                    }
                            }
                    }
                    break;
                case MAXBUFFERLENGTH: 
                    break;
            }
        }
        }
return replaced;
}	

bool isNotThaiTone(int unicode) {

    if (((unicode >= KO_KAI) && (unicode <= SARA_A)) 
        || ((unicode >= FONG_MAN) && (unicode <= KHO_MUT))
        || ((unicode >= SYMBOL_BAHT) && (unicode <= MAIYA_MOK))
        || (unicode == SARA_AA) ) {
#ifdef DEBUG_THAI_RESHAPER
        ALOGD("isNotThaiTone, true");
#endif
        return true;
    }
    return false;
}
//SAMSUNG CHANGE <<
