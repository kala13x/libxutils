/*!
 *  @file libxutils/src/crypt/aes.c
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Implementation of Advanced Encryption Standard
 * based on FIPS-197 implementation by Christophe Devine.
 */

/*
 *  FIPS-197 compliant AES implementation
 *
 *  Copyright (C) 2001-2004  Christophe Devine
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "aes.h"

#define XAES_GET_UINT32_BE(n,b,i)           \
{                                           \
    (n) = ((uint32_t)(b)[(i)    ] << 24)    \
        | ((uint32_t)(b)[(i) + 1] << 16)    \
        | ((uint32_t)(b)[(i) + 2] <<  8)    \
        | ((uint32_t)(b)[(i) + 3]      );   \
}

#define XAES_PUT_UINT32_BE(n,b,i)           \
{                                           \
    (b)[(i)]     = (uint8_t)((n) >> 24);    \
    (b)[(i) + 1] = (uint8_t)((n) >> 16);    \
    (b)[(i) + 2] = (uint8_t)((n) >>  8);    \
    (b)[(i) + 3] = (uint8_t)((n)      );    \
}

static const uint8_t fwdSBox[256] =
{
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5,
    0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76,
    0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0,
    0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0,
    0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC,
    0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A,
    0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
    0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0,
    0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84,
    0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B,
    0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85,
    0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
    0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5,
    0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
    0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17,
    0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88,
    0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB,
    0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C,
    0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
    0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9,
    0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6,
    0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A,
    0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E,
    0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E,
    0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94,
    0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68,
    0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16
};

/* Forward tables */
#define XAES_FT \
\
    X(C6,63,63,A5), X(F8,7C,7C,84), X(EE,77,77,99), X(F6,7B,7B,8D), \
    X(FF,F2,F2,0D), X(D6,6B,6B,BD), X(DE,6F,6F,B1), X(91,C5,C5,54), \
    X(60,30,30,50), X(02,01,01,03), X(CE,67,67,A9), X(56,2B,2B,7D), \
    X(E7,FE,FE,19), X(B5,D7,D7,62), X(4D,AB,AB,E6), X(EC,76,76,9A), \
    X(8F,CA,CA,45), X(1F,82,82,9D), X(89,C9,C9,40), X(FA,7D,7D,87), \
    X(EF,FA,FA,15), X(B2,59,59,EB), X(8E,47,47,C9), X(FB,F0,F0,0B), \
    X(41,AD,AD,EC), X(B3,D4,D4,67), X(5F,A2,A2,FD), X(45,AF,AF,EA), \
    X(23,9C,9C,BF), X(53,A4,A4,F7), X(E4,72,72,96), X(9B,C0,C0,5B), \
    X(75,B7,B7,C2), X(E1,FD,FD,1C), X(3D,93,93,AE), X(4C,26,26,6A), \
    X(6C,36,36,5A), X(7E,3F,3F,41), X(F5,F7,F7,02), X(83,CC,CC,4F), \
    X(68,34,34,5C), X(51,A5,A5,F4), X(D1,E5,E5,34), X(F9,F1,F1,08), \
    X(E2,71,71,93), X(AB,D8,D8,73), X(62,31,31,53), X(2A,15,15,3F), \
    X(08,04,04,0C), X(95,C7,C7,52), X(46,23,23,65), X(9D,C3,C3,5E), \
    X(30,18,18,28), X(37,96,96,A1), X(0A,05,05,0F), X(2F,9A,9A,B5), \
    X(0E,07,07,09), X(24,12,12,36), X(1B,80,80,9B), X(DF,E2,E2,3D), \
    X(CD,EB,EB,26), X(4E,27,27,69), X(7F,B2,B2,CD), X(EA,75,75,9F), \
    X(12,09,09,1B), X(1D,83,83,9E), X(58,2C,2C,74), X(34,1A,1A,2E), \
    X(36,1B,1B,2D), X(DC,6E,6E,B2), X(B4,5A,5A,EE), X(5B,A0,A0,FB), \
    X(A4,52,52,F6), X(76,3B,3B,4D), X(B7,D6,D6,61), X(7D,B3,B3,CE), \
    X(52,29,29,7B), X(DD,E3,E3,3E), X(5E,2F,2F,71), X(13,84,84,97), \
    X(A6,53,53,F5), X(B9,D1,D1,68), X(00,00,00,00), X(C1,ED,ED,2C), \
    X(40,20,20,60), X(E3,FC,FC,1F), X(79,B1,B1,C8), X(B6,5B,5B,ED), \
    X(D4,6A,6A,BE), X(8D,CB,CB,46), X(67,BE,BE,D9), X(72,39,39,4B), \
    X(94,4A,4A,DE), X(98,4C,4C,D4), X(B0,58,58,E8), X(85,CF,CF,4A), \
    X(BB,D0,D0,6B), X(C5,EF,EF,2A), X(4F,AA,AA,E5), X(ED,FB,FB,16), \
    X(86,43,43,C5), X(9A,4D,4D,D7), X(66,33,33,55), X(11,85,85,94), \
    X(8A,45,45,CF), X(E9,F9,F9,10), X(04,02,02,06), X(FE,7F,7F,81), \
    X(A0,50,50,F0), X(78,3C,3C,44), X(25,9F,9F,BA), X(4B,A8,A8,E3), \
    X(A2,51,51,F3), X(5D,A3,A3,FE), X(80,40,40,C0), X(05,8F,8F,8A), \
    X(3F,92,92,AD), X(21,9D,9D,BC), X(70,38,38,48), X(F1,F5,F5,04), \
    X(63,BC,BC,DF), X(77,B6,B6,C1), X(AF,DA,DA,75), X(42,21,21,63), \
    X(20,10,10,30), X(E5,FF,FF,1A), X(FD,F3,F3,0E), X(BF,D2,D2,6D), \
    X(81,CD,CD,4C), X(18,0C,0C,14), X(26,13,13,35), X(C3,EC,EC,2F), \
    X(BE,5F,5F,E1), X(35,97,97,A2), X(88,44,44,CC), X(2E,17,17,39), \
    X(93,C4,C4,57), X(55,A7,A7,F2), X(FC,7E,7E,82), X(7A,3D,3D,47), \
    X(C8,64,64,AC), X(BA,5D,5D,E7), X(32,19,19,2B), X(E6,73,73,95), \
    X(C0,60,60,A0), X(19,81,81,98), X(9E,4F,4F,D1), X(A3,DC,DC,7F), \
    X(44,22,22,66), X(54,2A,2A,7E), X(3B,90,90,AB), X(0B,88,88,83), \
    X(8C,46,46,CA), X(C7,EE,EE,29), X(6B,B8,B8,D3), X(28,14,14,3C), \
    X(A7,DE,DE,79), X(BC,5E,5E,E2), X(16,0B,0B,1D), X(AD,DB,DB,76), \
    X(DB,E0,E0,3B), X(64,32,32,56), X(74,3A,3A,4E), X(14,0A,0A,1E), \
    X(92,49,49,DB), X(0C,06,06,0A), X(48,24,24,6C), X(B8,5C,5C,E4), \
    X(9F,C2,C2,5D), X(BD,D3,D3,6E), X(43,AC,AC,EF), X(C4,62,62,A6), \
    X(39,91,91,A8), X(31,95,95,A4), X(D3,E4,E4,37), X(F2,79,79,8B), \
    X(D5,E7,E7,32), X(8B,C8,C8,43), X(6E,37,37,59), X(DA,6D,6D,B7), \
    X(01,8D,8D,8C), X(B1,D5,D5,64), X(9C,4E,4E,D2), X(49,A9,A9,E0), \
    X(D8,6C,6C,B4), X(AC,56,56,FA), X(F3,F4,F4,07), X(CF,EA,EA,25), \
    X(CA,65,65,AF), X(F4,7A,7A,8E), X(47,AE,AE,E9), X(10,08,08,18), \
    X(6F,BA,BA,D5), X(F0,78,78,88), X(4A,25,25,6F), X(5C,2E,2E,72), \
    X(38,1C,1C,24), X(57,A6,A6,F1), X(73,B4,B4,C7), X(97,C6,C6,51), \
    X(CB,E8,E8,23), X(A1,DD,DD,7C), X(E8,74,74,9C), X(3E,1F,1F,21), \
    X(96,4B,4B,DD), X(61,BD,BD,DC), X(0D,8B,8B,86), X(0F,8A,8A,85), \
    X(E0,70,70,90), X(7C,3E,3E,42), X(71,B5,B5,C4), X(CC,66,66,AA), \
    X(90,48,48,D8), X(06,03,03,05), X(F7,F6,F6,01), X(1C,0E,0E,12), \
    X(C2,61,61,A3), X(6A,35,35,5F), X(AE,57,57,F9), X(69,B9,B9,D0), \
    X(17,86,86,91), X(99,C1,C1,58), X(3A,1D,1D,27), X(27,9E,9E,B9), \
    X(D9,E1,E1,38), X(EB,F8,F8,13), X(2B,98,98,B3), X(22,11,11,33), \
    X(D2,69,69,BB), X(A9,D9,D9,70), X(07,8E,8E,89), X(33,94,94,A7), \
    X(2D,9B,9B,B6), X(3C,1E,1E,22), X(15,87,87,92), X(C9,E9,E9,20), \
    X(87,CE,CE,49), X(AA,55,55,FF), X(50,28,28,78), X(A5,DF,DF,7A), \
    X(03,8C,8C,8F), X(59,A1,A1,F8), X(09,89,89,80), X(1A,0D,0D,17), \
    X(65,BF,BF,DA), X(D7,E6,E6,31), X(84,42,42,C6), X(D0,68,68,B8), \
    X(82,41,41,C3), X(29,99,99,B0), X(5A,2D,2D,77), X(1E,0F,0F,11), \
    X(7B,B0,B0,CB), X(A8,54,54,FC), X(6D,BB,BB,D6), X(2C,16,16,3A)

#define X(a,b,c,d) 0x##a##b##c##d
static const uint32_t XAES_FT0[256] = { XAES_FT };
#undef X

#define X(a,b,c,d) 0x##d##a##b##c
static const uint32_t XAES_FT1[256] = { XAES_FT };
#undef X

#define X(a,b,c,d) 0x##c##d##a##b
static const uint32_t XAES_FT2[256] = { XAES_FT };
#undef X

#define X(a,b,c,d) 0x##b##c##d##a
static const uint32_t XAES_FT3[256] = { XAES_FT };
#undef X

#undef FT

/* Reverse S-box */
static const uint8_t rvSBox[256] =
{
    0x52, 0x09, 0x6A, 0xD5, 0x30, 0x36, 0xA5, 0x38,
    0xBF, 0x40, 0xA3, 0x9E, 0x81, 0xF3, 0xD7, 0xFB,
    0x7C, 0xE3, 0x39, 0x82, 0x9B, 0x2F, 0xFF, 0x87,
    0x34, 0x8E, 0x43, 0x44, 0xC4, 0xDE, 0xE9, 0xCB,
    0x54, 0x7B, 0x94, 0x32, 0xA6, 0xC2, 0x23, 0x3D,
    0xEE, 0x4C, 0x95, 0x0B, 0x42, 0xFA, 0xC3, 0x4E,
    0x08, 0x2E, 0xA1, 0x66, 0x28, 0xD9, 0x24, 0xB2,
    0x76, 0x5B, 0xA2, 0x49, 0x6D, 0x8B, 0xD1, 0x25,
    0x72, 0xF8, 0xF6, 0x64, 0x86, 0x68, 0x98, 0x16,
    0xD4, 0xA4, 0x5C, 0xCC, 0x5D, 0x65, 0xB6, 0x92,
    0x6C, 0x70, 0x48, 0x50, 0xFD, 0xED, 0xB9, 0xDA,
    0x5E, 0x15, 0x46, 0x57, 0xA7, 0x8D, 0x9D, 0x84,
    0x90, 0xD8, 0xAB, 0x00, 0x8C, 0xBC, 0xD3, 0x0A,
    0xF7, 0xE4, 0x58, 0x05, 0xB8, 0xB3, 0x45, 0x06,
    0xD0, 0x2C, 0x1E, 0x8F, 0xCA, 0x3F, 0x0F, 0x02,
    0xC1, 0xAF, 0xBD, 0x03, 0x01, 0x13, 0x8A, 0x6B,
    0x3A, 0x91, 0x11, 0x41, 0x4F, 0x67, 0xDC, 0xEA,
    0x97, 0xF2, 0xCF, 0xCE, 0xF0, 0xB4, 0xE6, 0x73,
    0x96, 0xAC, 0x74, 0x22, 0xE7, 0xAD, 0x35, 0x85,
    0xE2, 0xF9, 0x37, 0xE8, 0x1C, 0x75, 0xDF, 0x6E,
    0x47, 0xF1, 0x1A, 0x71, 0x1D, 0x29, 0xC5, 0x89,
    0x6F, 0xB7, 0x62, 0x0E, 0xAA, 0x18, 0xBE, 0x1B,
    0xFC, 0x56, 0x3E, 0x4B, 0xC6, 0xD2, 0x79, 0x20,
    0x9A, 0xDB, 0xC0, 0xFE, 0x78, 0xCD, 0x5A, 0xF4,
    0x1F, 0xDD, 0xA8, 0x33, 0x88, 0x07, 0xC7, 0x31,
    0xB1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xEC, 0x5F,
    0x60, 0x51, 0x7F, 0xA9, 0x19, 0xB5, 0x4A, 0x0D,
    0x2D, 0xE5, 0x7A, 0x9F, 0x93, 0xC9, 0x9C, 0xEF,
    0xA0, 0xE0, 0x3B, 0x4D, 0xAE, 0x2A, 0xF5, 0xB0,
    0xC8, 0xEB, 0xBB, 0x3C, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2B, 0x04, 0x7E, 0xBA, 0x77, 0xD6, 0x26,
    0xE1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0C, 0x7D
};

/* Reverse tables */
#define XAES_RT \
\
    X(51,F4,A7,50), X(7E,41,65,53), X(1A,17,A4,C3), X(3A,27,5E,96), \
    X(3B,AB,6B,CB), X(1F,9D,45,F1), X(AC,FA,58,AB), X(4B,E3,03,93), \
    X(20,30,FA,55), X(AD,76,6D,F6), X(88,CC,76,91), X(F5,02,4C,25), \
    X(4F,E5,D7,FC), X(C5,2A,CB,D7), X(26,35,44,80), X(B5,62,A3,8F), \
    X(DE,B1,5A,49), X(25,BA,1B,67), X(45,EA,0E,98), X(5D,FE,C0,E1), \
    X(C3,2F,75,02), X(81,4C,F0,12), X(8D,46,97,A3), X(6B,D3,F9,C6), \
    X(03,8F,5F,E7), X(15,92,9C,95), X(BF,6D,7A,EB), X(95,52,59,DA), \
    X(D4,BE,83,2D), X(58,74,21,D3), X(49,E0,69,29), X(8E,C9,C8,44), \
    X(75,C2,89,6A), X(F4,8E,79,78), X(99,58,3E,6B), X(27,B9,71,DD), \
    X(BE,E1,4F,B6), X(F0,88,AD,17), X(C9,20,AC,66), X(7D,CE,3A,B4), \
    X(63,DF,4A,18), X(E5,1A,31,82), X(97,51,33,60), X(62,53,7F,45), \
    X(B1,64,77,E0), X(BB,6B,AE,84), X(FE,81,A0,1C), X(F9,08,2B,94), \
    X(70,48,68,58), X(8F,45,FD,19), X(94,DE,6C,87), X(52,7B,F8,B7), \
    X(AB,73,D3,23), X(72,4B,02,E2), X(E3,1F,8F,57), X(66,55,AB,2A), \
    X(B2,EB,28,07), X(2F,B5,C2,03), X(86,C5,7B,9A), X(D3,37,08,A5), \
    X(30,28,87,F2), X(23,BF,A5,B2), X(02,03,6A,BA), X(ED,16,82,5C), \
    X(8A,CF,1C,2B), X(A7,79,B4,92), X(F3,07,F2,F0), X(4E,69,E2,A1), \
    X(65,DA,F4,CD), X(06,05,BE,D5), X(D1,34,62,1F), X(C4,A6,FE,8A), \
    X(34,2E,53,9D), X(A2,F3,55,A0), X(05,8A,E1,32), X(A4,F6,EB,75), \
    X(0B,83,EC,39), X(40,60,EF,AA), X(5E,71,9F,06), X(BD,6E,10,51), \
    X(3E,21,8A,F9), X(96,DD,06,3D), X(DD,3E,05,AE), X(4D,E6,BD,46), \
    X(91,54,8D,B5), X(71,C4,5D,05), X(04,06,D4,6F), X(60,50,15,FF), \
    X(19,98,FB,24), X(D6,BD,E9,97), X(89,40,43,CC), X(67,D9,9E,77), \
    X(B0,E8,42,BD), X(07,89,8B,88), X(E7,19,5B,38), X(79,C8,EE,DB), \
    X(A1,7C,0A,47), X(7C,42,0F,E9), X(F8,84,1E,C9), X(00,00,00,00), \
    X(09,80,86,83), X(32,2B,ED,48), X(1E,11,70,AC), X(6C,5A,72,4E), \
    X(FD,0E,FF,FB), X(0F,85,38,56), X(3D,AE,D5,1E), X(36,2D,39,27), \
    X(0A,0F,D9,64), X(68,5C,A6,21), X(9B,5B,54,D1), X(24,36,2E,3A), \
    X(0C,0A,67,B1), X(93,57,E7,0F), X(B4,EE,96,D2), X(1B,9B,91,9E), \
    X(80,C0,C5,4F), X(61,DC,20,A2), X(5A,77,4B,69), X(1C,12,1A,16), \
    X(E2,93,BA,0A), X(C0,A0,2A,E5), X(3C,22,E0,43), X(12,1B,17,1D), \
    X(0E,09,0D,0B), X(F2,8B,C7,AD), X(2D,B6,A8,B9), X(14,1E,A9,C8), \
    X(57,F1,19,85), X(AF,75,07,4C), X(EE,99,DD,BB), X(A3,7F,60,FD), \
    X(F7,01,26,9F), X(5C,72,F5,BC), X(44,66,3B,C5), X(5B,FB,7E,34), \
    X(8B,43,29,76), X(CB,23,C6,DC), X(B6,ED,FC,68), X(B8,E4,F1,63), \
    X(D7,31,DC,CA), X(42,63,85,10), X(13,97,22,40), X(84,C6,11,20), \
    X(85,4A,24,7D), X(D2,BB,3D,F8), X(AE,F9,32,11), X(C7,29,A1,6D), \
    X(1D,9E,2F,4B), X(DC,B2,30,F3), X(0D,86,52,EC), X(77,C1,E3,D0), \
    X(2B,B3,16,6C), X(A9,70,B9,99), X(11,94,48,FA), X(47,E9,64,22), \
    X(A8,FC,8C,C4), X(A0,F0,3F,1A), X(56,7D,2C,D8), X(22,33,90,EF), \
    X(87,49,4E,C7), X(D9,38,D1,C1), X(8C,CA,A2,FE), X(98,D4,0B,36), \
    X(A6,F5,81,CF), X(A5,7A,DE,28), X(DA,B7,8E,26), X(3F,AD,BF,A4), \
    X(2C,3A,9D,E4), X(50,78,92,0D), X(6A,5F,CC,9B), X(54,7E,46,62), \
    X(F6,8D,13,C2), X(90,D8,B8,E8), X(2E,39,F7,5E), X(82,C3,AF,F5), \
    X(9F,5D,80,BE), X(69,D0,93,7C), X(6F,D5,2D,A9), X(CF,25,12,B3), \
    X(C8,AC,99,3B), X(10,18,7D,A7), X(E8,9C,63,6E), X(DB,3B,BB,7B), \
    X(CD,26,78,09), X(6E,59,18,F4), X(EC,9A,B7,01), X(83,4F,9A,A8), \
    X(E6,95,6E,65), X(AA,FF,E6,7E), X(21,BC,CF,08), X(EF,15,E8,E6), \
    X(BA,E7,9B,D9), X(4A,6F,36,CE), X(EA,9F,09,D4), X(29,B0,7C,D6), \
    X(31,A4,B2,AF), X(2A,3F,23,31), X(C6,A5,94,30), X(35,A2,66,C0), \
    X(74,4E,BC,37), X(FC,82,CA,A6), X(E0,90,D0,B0), X(33,A7,D8,15), \
    X(F1,04,98,4A), X(41,EC,DA,F7), X(7F,CD,50,0E), X(17,91,F6,2F), \
    X(76,4D,D6,8D), X(43,EF,B0,4D), X(CC,AA,4D,54), X(E4,96,04,DF), \
    X(9E,D1,B5,E3), X(4C,6A,88,1B), X(C1,2C,1F,B8), X(46,65,51,7F), \
    X(9D,5E,EA,04), X(01,8C,35,5D), X(FA,87,74,73), X(FB,0B,41,2E), \
    X(B3,67,1D,5A), X(92,DB,D2,52), X(E9,10,56,33), X(6D,D6,47,13), \
    X(9A,D7,61,8C), X(37,A1,0C,7A), X(59,F8,14,8E), X(EB,13,3C,89), \
    X(CE,A9,27,EE), X(B7,61,C9,35), X(E1,1C,E5,ED), X(7A,47,B1,3C), \
    X(9C,D2,DF,59), X(55,F2,73,3F), X(18,14,CE,79), X(73,C7,37,BF), \
    X(53,F7,CD,EA), X(5F,FD,AA,5B), X(DF,3D,6F,14), X(78,44,DB,86), \
    X(CA,AF,F3,81), X(B9,68,C4,3E), X(38,24,34,2C), X(C2,A3,40,5F), \
    X(16,1D,C3,72), X(BC,E2,25,0C), X(28,3C,49,8B), X(FF,0D,95,41), \
    X(39,A8,01,71), X(08,0C,B3,DE), X(D8,B4,E4,9C), X(64,56,C1,90), \
    X(7B,CB,84,61), X(D5,32,B6,70), X(48,6C,5C,74), X(D0,B8,57,42)

#define X(a,b,c,d) 0x##a##b##c##d
static const uint32_t XAES_RT0[256] = { XAES_RT };
#undef X

#define X(a,b,c,d) 0x##d##a##b##c
static const uint32_t XAES_RT1[256] = { XAES_RT };
#undef X

#define X(a,b,c,d) 0x##c##d##a##b
static const uint32_t XAES_RT2[256] = { XAES_RT };
#undef X

#define X(a,b,c,d) 0x##b##c##d##a
static const uint32_t XAES_RT3[256] = { XAES_RT };
#undef X

#undef XAES_RT

static const uint32_t XAES_RoundConsts[10] =
{
    0x01000000, 0x02000000, 0x04000000, 0x08000000,
    0x10000000, 0x20000000, 0x40000000, 0x80000000,
    0x1B000000, 0x36000000
};

/* Decryption key schedule tables */
static uint32_t XAES_KT0[256];
static uint32_t XAES_KT1[256];
static uint32_t XAES_KT2[256];
static uint32_t XAES_KT3[256];
static uint8_t g_nKTInit = 0;

void XAES_SetKey(xaes_context_t *pCtx, const uint8_t *pKey, size_t nSize, const uint8_t *pIV)
{
    if (pIV == NULL) memset((void*)pCtx->IV, 0, sizeof(pCtx->IV));
    else memcpy((void*)pCtx->IV, (void*)pIV, sizeof(pCtx->IV));

    switch (nSize)
    {
        case 128: pCtx->nRounds = 10; break;
        case 192: pCtx->nRounds = 12; break;
        case 256: pCtx->nRounds = 14; break;
        default : return;
    }

    uint32_t *RK = pCtx->encKeys;
    int i;

    for (i = 0; i < (nSize >> 5); i++)
        XAES_GET_UINT32_BE(RK[i], pKey, i << 2);

    switch (pCtx->nRounds)
    {
        case 10:
            for (i = 0; i < 10; i++, RK += 4)
            {
                RK[4] = RK[0] ^ XAES_RoundConsts[i] ^
                    (fwdSBox[(uint8_t)(RK[3] >> 16)] << 24) ^
                    (fwdSBox[(uint8_t)(RK[3] >>  8)] << 16) ^
                    (fwdSBox[(uint8_t)(RK[3]      )] <<  8) ^
                    (fwdSBox[(uint8_t)(RK[3] >> 24)]      );

                RK[5]  = RK[1] ^ RK[4];
                RK[6]  = RK[2] ^ RK[5];
                RK[7]  = RK[3] ^ RK[6];
            }
            break;
        case 12:
            for (i = 0; i < 8; i++, RK += 6)
            {
                RK[6] = RK[0] ^ XAES_RoundConsts[i] ^
                    (fwdSBox[(uint8_t)(RK[5] >> 16)] << 24) ^
                    (fwdSBox[(uint8_t)(RK[5] >>  8)] << 16) ^
                    (fwdSBox[(uint8_t)(RK[5]      )] <<  8) ^
                    (fwdSBox[(uint8_t)(RK[5] >> 24)]      );

                RK[7]  = RK[1] ^ RK[6];
                RK[8]  = RK[2] ^ RK[7];
                RK[9]  = RK[3] ^ RK[8];
                RK[10] = RK[4] ^ RK[9];
                RK[11] = RK[5] ^ RK[10];
            }
            break;
        case 14:

            for (i = 0; i < 7; i++, RK += 8)
            {
                RK[8]  = RK[0] ^ XAES_RoundConsts[i] ^
                    (fwdSBox[(uint8_t)(RK[7] >> 16)] << 24) ^
                    (fwdSBox[(uint8_t)(RK[7] >>  8)] << 16) ^
                    (fwdSBox[(uint8_t)(RK[7]      )] <<  8) ^
                    (fwdSBox[(uint8_t)(RK[7] >> 24)]      );

                RK[9]  = RK[1] ^ RK[8];
                RK[10] = RK[2] ^ RK[9];
                RK[11] = RK[3] ^ RK[10];

                RK[12] = RK[4] ^
                    (fwdSBox[(uint8_t)(RK[11] >> 24)] << 24) ^
                    (fwdSBox[(uint8_t)(RK[11] >> 16)] << 16) ^
                    (fwdSBox[(uint8_t)(RK[11] >>  8)] <<  8) ^
                    (fwdSBox[(uint8_t)(RK[11]      )]      );

                RK[13] = RK[5] ^ RK[12];
                RK[14] = RK[6] ^ RK[13];
                RK[15] = RK[7] ^ RK[14];
            }
            break;
        default:
            break;
    }

    /* Setup decryption round keys */
    if (!g_nKTInit)
    {
        for (i = 0; i < 256; i++)
        {
            XAES_KT0[i] = XAES_RT0[fwdSBox[i]];
            XAES_KT1[i] = XAES_RT1[fwdSBox[i]];
            XAES_KT2[i] = XAES_RT2[fwdSBox[i]];
            XAES_KT3[i] = XAES_RT3[fwdSBox[i]];
        }

        g_nKTInit = 1;
    }

    uint32_t *SK = pCtx->decKeys;
    *SK++ = *RK++;
    *SK++ = *RK++;
    *SK++ = *RK++;
    *SK++ = *RK++;

    for (i = 1; i < pCtx->nRounds; i++)
    {
        RK -= 8;

        *SK++ = XAES_KT0[(uint8_t)(*RK >> 24)] ^
                XAES_KT1[(uint8_t)(*RK >> 16)] ^
                XAES_KT2[(uint8_t)(*RK >>  8)] ^
                XAES_KT3[(uint8_t)(*RK      )]; RK++;

        *SK++ = XAES_KT0[(uint8_t)(*RK >> 24)] ^
                XAES_KT1[(uint8_t)(*RK >> 16)] ^
                XAES_KT2[(uint8_t)(*RK >>  8)] ^
                XAES_KT3[(uint8_t)(*RK      )]; RK++;

        *SK++ = XAES_KT0[(uint8_t)(*RK >> 24)] ^
                XAES_KT1[(uint8_t)(*RK >> 16)] ^
                XAES_KT2[(uint8_t)(*RK >>  8)] ^
                XAES_KT3[(uint8_t)(*RK      )]; RK++;

        *SK++ = XAES_KT0[(uint8_t)(*RK >> 24)] ^
                XAES_KT1[(uint8_t)(*RK >> 16)] ^
                XAES_KT2[(uint8_t)(*RK >>  8)] ^
                XAES_KT3[(uint8_t)(*RK      )]; RK++;
    }

    RK -= 8;
    *SK++ = *RK++;
    *SK++ = *RK++;
    *SK++ = *RK++;
    *SK++ = *RK++;
}

void XAES_EncryptBlock(xaes_context_t *pCtx, uint8_t output[XAES_BLOCK_SIZE], const uint8_t input[XAES_BLOCK_SIZE])
{
    uint32_t X0, X1, X2, X3, Y0, Y1, Y2, Y3;
    uint32_t *RK = pCtx->encKeys;

    XAES_GET_UINT32_BE(X0, input,  0); X0 ^= RK[0];
    XAES_GET_UINT32_BE(X1, input,  4); X1 ^= RK[1];
    XAES_GET_UINT32_BE(X2, input,  8); X2 ^= RK[2];
    XAES_GET_UINT32_BE(X3, input, 12); X3 ^= RK[3];

#define XAES_FROUND(X0,X1,X2,X3,Y0,Y1,Y2,Y3)        \
{                                                   \
    RK += 4;                                        \
                                                    \
    X0 = RK[0] ^ XAES_FT0[(uint8_t)(Y0 >> 24)] ^    \
                 XAES_FT1[(uint8_t)(Y1 >> 16)] ^    \
                 XAES_FT2[(uint8_t)(Y2 >>  8)] ^    \
                 XAES_FT3[(uint8_t)(Y3      )];     \
                                                    \
    X1 = RK[1] ^ XAES_FT0[(uint8_t)(Y1 >> 24)] ^    \
                 XAES_FT1[(uint8_t)(Y2 >> 16)] ^    \
                 XAES_FT2[(uint8_t)(Y3 >>  8)] ^    \
                 XAES_FT3[(uint8_t)(Y0      )];     \
                                                    \
    X2 = RK[2] ^ XAES_FT0[(uint8_t)(Y2 >> 24)] ^    \
                 XAES_FT1[(uint8_t)(Y3 >> 16)] ^    \
                 XAES_FT2[(uint8_t)(Y0 >>  8)] ^    \
                 XAES_FT3[(uint8_t)(Y1      )];     \
                                                    \
    X3 = RK[3] ^ XAES_FT0[(uint8_t)(Y3 >> 24)] ^    \
                 XAES_FT1[(uint8_t)(Y0 >> 16)] ^    \
                 XAES_FT2[(uint8_t)(Y1 >>  8)] ^    \
                 XAES_FT3[(uint8_t)(Y2      )];     \
}

    XAES_FROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);
    XAES_FROUND(X0, X1, X2, X3, Y0, Y1, Y2, Y3);
    XAES_FROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);
    XAES_FROUND(X0, X1, X2, X3, Y0, Y1, Y2, Y3);
    XAES_FROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);
    XAES_FROUND(X0, X1, X2, X3, Y0, Y1, Y2, Y3);
    XAES_FROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);
    XAES_FROUND(X0, X1, X2, X3, Y0, Y1, Y2, Y3);
    XAES_FROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);

    if (pCtx->nRounds > 10)
    {
        XAES_FROUND(X0, X1, X2, X3, Y0, Y1, Y2, Y3);
        XAES_FROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);
    }

    if (pCtx->nRounds > 12)
    {
        XAES_FROUND(X0, X1, X2, X3, Y0, Y1, Y2, Y3);
        XAES_FROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);
    }

    RK += 4;

    X0 = RK[0] ^ (fwdSBox[(uint8_t)(Y0 >> 24)] << 24) ^
                 (fwdSBox[(uint8_t)(Y1 >> 16)] << 16) ^
                 (fwdSBox[(uint8_t)(Y2 >>  8)] <<  8) ^
                 (fwdSBox[(uint8_t)(Y3      )]      );

    X1 = RK[1] ^ (fwdSBox[(uint8_t)(Y1 >> 24)] << 24) ^
                 (fwdSBox[(uint8_t)(Y2 >> 16)] << 16) ^
                 (fwdSBox[(uint8_t)(Y3 >>  8)] <<  8) ^
                 (fwdSBox[(uint8_t)(Y0      )]      );

    X2 = RK[2] ^ (fwdSBox[(uint8_t)(Y2 >> 24)] << 24) ^
                 (fwdSBox[(uint8_t)(Y3 >> 16)] << 16) ^
                 (fwdSBox[(uint8_t)(Y0 >>  8)] <<  8) ^
                 (fwdSBox[(uint8_t)(Y1      )]      );

    X3 = RK[3] ^ (fwdSBox[(uint8_t)(Y3 >> 24)] << 24) ^
                 (fwdSBox[(uint8_t)(Y0 >> 16)] << 16) ^
                 (fwdSBox[(uint8_t)(Y1 >>  8)] <<  8) ^
                 (fwdSBox[(uint8_t)(Y2      )]      );

    XAES_PUT_UINT32_BE(X0, output,  0);
    XAES_PUT_UINT32_BE(X1, output,  4);
    XAES_PUT_UINT32_BE(X2, output,  8);
    XAES_PUT_UINT32_BE(X3, output, 12);
}

void XAES_DecryptBlock(xaes_context_t *pCtx, uint8_t output[XAES_BLOCK_SIZE], const uint8_t input[XAES_BLOCK_SIZE])
{
    uint32_t X0, X1, X2, X3, Y0, Y1, Y2, Y3;
    uint32_t *RK = pCtx->decKeys;

    XAES_GET_UINT32_BE(X0, input,  0); X0 ^= RK[0];
    XAES_GET_UINT32_BE(X1, input,  4); X1 ^= RK[1];
    XAES_GET_UINT32_BE(X2, input,  8); X2 ^= RK[2];
    XAES_GET_UINT32_BE(X3, input, 12); X3 ^= RK[3];

#define XAES_RROUND(X0,X1,X2,X3,Y0,Y1,Y2,Y3)        \
{                                                   \
    RK += 4;                                        \
                                                    \
    X0 = RK[0] ^ XAES_RT0[(uint8_t)(Y0 >> 24)] ^    \
                 XAES_RT1[(uint8_t)(Y3 >> 16)] ^    \
                 XAES_RT2[(uint8_t)(Y2 >>  8)] ^    \
                 XAES_RT3[(uint8_t)(Y1      )];     \
                                                    \
    X1 = RK[1] ^ XAES_RT0[(uint8_t)(Y1 >> 24)] ^    \
                 XAES_RT1[(uint8_t)(Y0 >> 16)] ^    \
                 XAES_RT2[(uint8_t)(Y3 >>  8)] ^    \
                 XAES_RT3[(uint8_t)(Y2      )];     \
                                                    \
    X2 = RK[2] ^ XAES_RT0[(uint8_t)(Y2 >> 24)] ^    \
                 XAES_RT1[(uint8_t)(Y1 >> 16)] ^    \
                 XAES_RT2[(uint8_t)(Y0 >>  8)] ^    \
                 XAES_RT3[(uint8_t)(Y3      )];     \
                                                    \
    X3 = RK[3] ^ XAES_RT0[(uint8_t)(Y3 >> 24)] ^    \
                 XAES_RT1[(uint8_t)(Y2 >> 16)] ^    \
                 XAES_RT2[(uint8_t)(Y1 >>  8)] ^    \
                 XAES_RT3[(uint8_t)(Y0      )];     \
}

    XAES_RROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);
    XAES_RROUND(X0, X1, X2, X3, Y0, Y1, Y2, Y3);
    XAES_RROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);
    XAES_RROUND(X0, X1, X2, X3, Y0, Y1, Y2, Y3);
    XAES_RROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);
    XAES_RROUND(X0, X1, X2, X3, Y0, Y1, Y2, Y3);
    XAES_RROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);
    XAES_RROUND(X0, X1, X2, X3, Y0, Y1, Y2, Y3);
    XAES_RROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);

    if (pCtx->nRounds > 10)
    {
        XAES_RROUND(X0, X1, X2, X3, Y0, Y1, Y2, Y3);
        XAES_RROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);
    }

    if (pCtx->nRounds > 12)
    {
        XAES_RROUND(X0, X1, X2, X3, Y0, Y1, Y2, Y3);
        XAES_RROUND(Y0, Y1, Y2, Y3, X0, X1, X2, X3);
    }

    RK += 4;
    X0 = RK[0] ^ (rvSBox[(uint8_t)(Y0 >> 24)] << 24) ^
                 (rvSBox[(uint8_t)(Y3 >> 16)] << 16) ^
                 (rvSBox[(uint8_t)(Y2 >>  8)] <<  8) ^
                 (rvSBox[(uint8_t)(Y1      )]     );

    X1 = RK[1] ^ (rvSBox[(uint8_t)(Y1 >> 24)] << 24) ^
                 (rvSBox[(uint8_t)(Y0 >> 16)] << 16) ^
                 (rvSBox[(uint8_t)(Y3 >>  8)] <<  8) ^
                 (rvSBox[(uint8_t)(Y2      )]      );

    X2 = RK[2] ^ (rvSBox[(uint8_t)(Y2 >> 24)] << 24) ^
                 (rvSBox[(uint8_t)(Y1 >> 16)] << 16) ^
                 (rvSBox[(uint8_t)(Y0 >>  8)] <<  8) ^
                 (rvSBox[(uint8_t)(Y3      )]      );

    X3 = RK[3] ^ (rvSBox[(uint8_t)(Y3 >> 24)] << 24) ^
                 (rvSBox[(uint8_t)(Y2 >> 16)] << 16) ^
                 (rvSBox[(uint8_t)(Y1 >>  8)] <<  8) ^
                 (rvSBox[(uint8_t)(Y0      )]      );

    XAES_PUT_UINT32_BE(X0, output,  0);
    XAES_PUT_UINT32_BE(X1, output,  4);
    XAES_PUT_UINT32_BE(X2, output,  8);
    XAES_PUT_UINT32_BE(X3, output, 12);
}

////////////////////////////////////////////////////////////////////////////////////////
// libxutils implementation to encode and decode input buffers of various sizes
////////////////////////////////////////////////////////////////////////////////////////

uint8_t* XAES_Encrypt(xaes_context_t *pCtx, const uint8_t *pInput, size_t *pLength)
{
    if (pInput == NULL || pLength == NULL || !(*pLength)) return NULL;
    uint8_t *pInputPtr = NULL;
    size_t nDataLength = 0;

    uint8_t iv[XAES_BLOCK_SIZE];
    memcpy(iv, pCtx->IV, sizeof(iv));

    if (*pLength % XAES_BLOCK_SIZE)
    {
        size_t nCount = *pLength / XAES_BLOCK_SIZE;
        size_t nPart = *pLength - nCount * XAES_BLOCK_SIZE;
        nDataLength = *pLength + (XAES_BLOCK_SIZE - nPart);
    }

    if (nDataLength)
    {
        pInputPtr = (uint8_t*)malloc(nDataLength + 1);
        if (pInputPtr == NULL) return NULL;

        memcpy(pInputPtr, pInput, *pLength);
        memset(pInputPtr + *pLength, 0, nDataLength - *pLength);
    }

    const uint8_t *pData = pInputPtr != NULL ? pInputPtr : pInput;
    nDataLength = nDataLength ? nDataLength : *pLength;

    uint8_t *pOutput = (uint8_t*)malloc(nDataLength + 1);
    if (pOutput == NULL)
    {
        free(pInputPtr);
        return NULL;
    }

    uint8_t *pOffset = pOutput;
    size_t nDataLeft = nDataLength;

    while (nDataLeft > 0)
    {
        uint8_t i;
        for (i = 0; i < XAES_BLOCK_SIZE; i++)
            pOffset[i] = pData[i] ^ iv[i];

        XAES_EncryptBlock(pCtx, pOffset, pOffset);
        memcpy(iv, pOffset, XAES_BLOCK_SIZE);

        nDataLeft -= XAES_BLOCK_SIZE;
        pOffset += XAES_BLOCK_SIZE;
        pData += XAES_BLOCK_SIZE;
    }

    *pLength = nDataLength;
    pOutput[*pLength] = '\0';

    free(pInputPtr);
    return pOutput;
}

uint8_t* XAES_Decrypt(xaes_context_t *pCtx, const  uint8_t *pInput, size_t *pLength)
{
    if (pInput == NULL || pLength == NULL || !(*pLength)) return NULL;
    uint8_t *pInputPtr = NULL;
    size_t nDataLength = 0;

    uint8_t iv[XAES_BLOCK_SIZE];
    memcpy(iv, pCtx->IV, sizeof(iv));

    if (*pLength % XAES_BLOCK_SIZE)
    {
        size_t nCount = *pLength / XAES_BLOCK_SIZE;
        size_t nPart = *pLength - nCount * XAES_BLOCK_SIZE;
        nDataLength = *pLength + (XAES_BLOCK_SIZE - nPart);
    }

    if (nDataLength)
    {
        pInputPtr = (uint8_t*)malloc(nDataLength + 1);
        if (pInputPtr == NULL) return NULL;

        memcpy(pInputPtr, pInput, *pLength);
        memset(pInputPtr + *pLength, 0, nDataLength - *pLength);
    }

    const uint8_t *pData = pInputPtr != NULL ? pInputPtr : pInput;
    nDataLength = nDataLength ? nDataLength : *pLength;

    uint8_t *pOutput = (uint8_t*)malloc(nDataLength + 1);
    if (pOutput == NULL)
    {
        free(pInputPtr);
        return NULL;
    }

    uint8_t *pOffset = pOutput;
    size_t nDataLeft = nDataLength;

    while (nDataLeft > 0)
    {
        uint8_t i, ivTmp[XAES_BLOCK_SIZE];
        memcpy(ivTmp, pData, XAES_BLOCK_SIZE);
        XAES_DecryptBlock(pCtx, pOffset, pData);

        for (i = 0; i < XAES_BLOCK_SIZE; i++)
            pOffset[i] = pOffset[i] ^ iv[i];

        memcpy(iv, ivTmp, XAES_BLOCK_SIZE);
        nDataLeft -= XAES_BLOCK_SIZE;
        pOffset += XAES_BLOCK_SIZE;
        pData += XAES_BLOCK_SIZE;
    }

    pOutput[nDataLength] = '\0';
    *pLength = nDataLength;

    while (nDataLength && pOutput[nDataLength] == '\0') nDataLength--;
    if (*pLength != nDataLength) *pLength = nDataLength + 1;

    free(pInputPtr);
    return pOutput;
}

////////////////////////////////////////////////////////////////////////////////////////
// END OF: libxutils implementation to encode and decode input buffers of various sizes
////////////////////////////////////////////////////////////////////////////////////////