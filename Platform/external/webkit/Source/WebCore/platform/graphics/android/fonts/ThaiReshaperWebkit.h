/*
 * Copyright (C) 2006 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License")
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

/*
 * uncomment to enable Thai reshaper logs
 */
//#define THAIRESHAPERLOGS

#define THAIRESHAPEREMOVETAIL 0
#define THAIRESHAPEPULLDOWN 1
#define THAIRESHAPESHIFTLEFT 2
#define NOTTHAIRESHAPE -1
#define MAXBUFFERLENGTH 4
#define MAXPREVBUFFERLENGTH 3

//Thai unicodes
//Normal
#define KO_KAI 0xE01
#define KHO_MUT  0xE5B
#define SARA_A  0xE30
#define FONG_MAN  0xE4F
#define SYMBOL_BAHT  0xE3F
#define MAIYA_MOK  0xE46
#define LO_CHULLA  0xE2C

// Under
#define SARA_U  0xE38
#define SARA_UU  0xE39
#define PHINTHU  0xE3A

// Under after pullDown
#define SARA_U_DOWN  0xF718
#define SARA_UU_DOWN  0xF719
#define PHINTHU_DOWN  0xF71A

// Upper level 1
#define MAI_HAN_AKAT  0xE31
#define SARA_AM_FIRST  0xE32
#define SARA_AM  0xE33
#define SARA_I  0xE34
#define SARA_Ii  0xE35
#define SARA_Ue  0xE36
#define SARA_Uee  0xE37
#define MAI_TAI_KHU  0xE47

// Upper level 1 after left shift
#define MAI_HAN_AKAT_LEFT_SHIFT  0xF710
#define SARA_I_LEFT_SHIFT  0xF701
#define SARA_Ii_LEFT_SHIFT  0xF702
#define SARA_Ue_LEFT_SHIFT  0xF703
#define SARA_Uee_LEFT_SHIFT  0xF704
#define MAI_TAI_KHU_LEFT_SHIFT  0xF712
#define SARA_AM_LEFT_SHIFT 0xF71B

// Upper level 2
#define MAI_EK  0xE48
#define MAI_THO  0xE49
#define MAI_TRI  0xE4A
#define MAI_CHATTAWA  0xE4B
#define THANTHAKHAT  0xE4C
#define NIKHAHIT  0xE4D

// Upper level 2 after pull down
#define MAI_EK_DOWN  0xF70A
#define MAI_THO_DOWN  0xF70B
#define MAI_TRI_DOWN  0xF70C
#define MAI_CHATTAWA_DOWN  0xF70D
#define THANTHAKHAT_DOWN  0xF70E

// Upper level 2 after shift left
#define MAI_EK_LEFT_SHIFT  0xF713
#define MAI_THO_LEFT_SHIFT  0xF714
#define MAI_TRI_LEFT_SHIFT  0xF715
#define MAI_CHATTAWA_LEFT_SHIFT  0xF716
#define THANTHAKHAT_LEFT_SHIFT  0xF717
#define NIKHAHIT_LEFT_SHIFT  0xF711

// Up tail
#define PO_PLA  0x0E1B
#define FO_FA  0x0E1D
#define FO_FAN  0x0E1F

// Down tail
#define THO_THAN  0xE10
#define YO_YING  0xE0D
#define DO_CHADA  0xE0E
#define TO_PATAK  0xE0F
#define RU  0xE24
#define LU  0xE26

// Cut tail
#define THO_THAN_CUT_TAIL  0xF700
#define YO_YING_CUT_TAIL  0xF70F

#define SARA_AA  0xE32

#include <stddef.h>
#include <stdint.h>

#include <unicode/umachine.h>
bool isThai(unsigned short uniCode);///checked

bool isNotThaiTone(int unicode);

int isThaiReshaperCharhw(int32_t c); ///checked
int isThaiReshaperTonehw(int32_t uniCode, int32_t consonantType);

bool thaiReshaperInPlace(UChar** , size_t );
