#ifndef __DINPUT_H__
#define __DINPUT_H__

/*
** Emscripten stub for DirectInput 8
**
** On Windows, the game uses DirectInput for keyboard input.
** On Emscripten, this header provides type-compatible stubs that compile
** against the original DirectInputKeyboard code. The GetDeviceData method
** reads from a global ring buffer that is filled by EmscriptenInput.cpp
** (which translates SDL2 scancodes to DIK_* codes).
**
** GeneralsX @feature WebPort 09/03/2026 — initial stub
** GeneralsX @feature WebPort 15/03/2026 — functional SDL input bridge
*/

// ============================================================================
// DirectInput Key Scan Codes (IBM PC AT Set 1)
// ============================================================================

#define DIK_ESCAPE 0x01
#define DIK_1 0x02
#define DIK_2 0x03
#define DIK_3 0x04
#define DIK_4 0x05
#define DIK_5 0x06
#define DIK_6 0x07
#define DIK_7 0x08
#define DIK_8 0x09
#define DIK_9 0x0A
#define DIK_0 0x0B
#define DIK_MINUS 0x0C /* - on main keyboard */
#define DIK_EQUALS 0x0D
#define DIK_BACK 0x0E /* backspace */
#define DIK_TAB 0x0F
#define DIK_Q 0x10
#define DIK_W 0x11
#define DIK_E 0x12
#define DIK_R 0x13
#define DIK_T 0x14
#define DIK_Y 0x15
#define DIK_U 0x16
#define DIK_I 0x17
#define DIK_O 0x18
#define DIK_P 0x19
#define DIK_LBRACKET 0x1A
#define DIK_RBRACKET 0x1B
#define DIK_RETURN 0x1C /* Enter on main keyboard */
#define DIK_LCONTROL 0x1D
#define DIK_A 0x1E
#define DIK_S 0x1F
#define DIK_D 0x20
#define DIK_F 0x21
#define DIK_G 0x22
#define DIK_H 0x23
#define DIK_J 0x24
#define DIK_K 0x25
#define DIK_L 0x26
#define DIK_SEMICOLON 0x27
#define DIK_APOSTROPHE 0x28
#define DIK_GRAVE 0x29 /* accent grave */
#define DIK_LSHIFT 0x2A
#define DIK_BACKSLASH 0x2B
#define DIK_Z 0x2C
#define DIK_X 0x2D
#define DIK_C 0x2E
#define DIK_V 0x2F
#define DIK_B 0x30
#define DIK_N 0x31
#define DIK_M 0x32
#define DIK_COMMA 0x33
#define DIK_PERIOD 0x34 /* . on main keyboard */
#define DIK_SLASH 0x35  /* / on main keyboard */
#define DIK_RSHIFT 0x36
#define DIK_NUMPADSTAR 0x37 /* * on numeric keypad */
#define DIK_LALT 0x38       /* left Alt */
#define DIK_SPACE 0x39
#define DIK_CAPSLOCK 0x3A
#define DIK_F1 0x3B
#define DIK_F2 0x3C
#define DIK_F3 0x3D
#define DIK_F4 0x3E
#define DIK_F5 0x3F
#define DIK_F6 0x40
#define DIK_F7 0x41
#define DIK_F8 0x42
#define DIK_F9 0x43
#define DIK_F10 0x44
#define DIK_NUMLOCK 0x45
#define DIK_SCROLL 0x46 /* Scroll Lock */
#define DIK_NUMPAD7 0x47
#define DIK_NUMPAD8 0x48
#define DIK_NUMPAD9 0x49
#define DIK_NUMPADMINUS 0x4A /* - on numeric keypad */
#define DIK_NUMPAD4 0x4B
#define DIK_NUMPAD5 0x4C
#define DIK_NUMPAD6 0x4D
#define DIK_NUMPADPLUS 0x4E /* + on numeric keypad */
#define DIK_NUMPAD1 0x4F
#define DIK_NUMPAD2 0x50
#define DIK_NUMPAD3 0x51
#define DIK_NUMPAD0 0x52
#define DIK_NUMPADPERIOD 0x53 /* . on numeric keypad */
#define DIK_OEM_102 0x56      /* < > | on UK/Germany keyboards */
#define DIK_F11 0x57
#define DIK_F12 0x58
#define DIK_KANA 0x70        /* (Japanese keyboard)            */
#define DIK_CONVERT 0x79     /* (Japanese keyboard)            */
#define DIK_NOCONVERT 0x7B   /* (Japanese keyboard)            */
#define DIK_YEN 0x7D         /* (Japanese keyboard)            */
#define DIK_NUMPADENTER 0x9C /* Enter on numeric keypad */
#define DIK_RCONTROL 0x9D
#define DIK_NUMPADSLASH 0xB5 /* / on numeric keypad */
#define DIK_SYSRQ 0xB7
#define DIK_RALT 0xB8       /* right Alt */
#define DIK_HOME 0xC7       /* Home on arrow keypad */
#define DIK_UPARROW 0xC8    /* UpArrow on arrow keypad */
#define DIK_PGUP 0xC9       /* PgUp on arrow keypad */
#define DIK_LEFTARROW 0xCB  /* LeftArrow on arrow keypad */
#define DIK_RIGHTARROW 0xCD /* RightArrow on arrow keypad */
#define DIK_END 0xCF        /* End on arrow keypad */
#define DIK_DOWNARROW 0xD0  /* DownArrow on arrow keypad */
#define DIK_PGDN 0xD1       /* PgDn on arrow keypad */
#define DIK_INSERT 0xD2     /* Insert on arrow keypad */
#define DIK_DELETE 0xD3     /* Delete on arrow keypad */
#define DIK_CIRCUMFLEX 0x90 /* (Japanese keyboard)            */
#define DIK_KANJI 0x94      /* (Japanese keyboard)            */

// ============================================================================
// DirectInput Error Codes
// ============================================================================

#define DIERR_ACQUIRED 0x80040200
#define DIERR_ALREADYINITIALIZED 0x80040201
#define DIERR_BADDRIVERVER 0x80040202
#define DIERR_BETADIRECTINPUTVERSION 0x80040203
#define DIERR_DEVICEFULL 0x80040204
#define DIERR_DEVICENOTREG 0x80040205
#define DIERR_EFFECTPLAYING 0x80040206
#define DIERR_GENERIC (long)0x80040207
#define DIERR_HANDLEEXISTS (long)0x80040208
#define DIERR_HASEFFECTS (long)0x80040209
#define DIERR_INCOMPLETEEFFECT (long)0x8004020A
#define DIERR_INPUTLOST (long)0x8004020B
#define DIERR_INVALIDPARAM (long)0x8004020C
#define DIERR_MAPFILEFAIL (long)0x8004020D
#define DIERR_MOREDATA (long)0x8004020E
#define DIERR_NOAGGREGATION (long)0x8004020F
#define DIERR_NOINTERFACE (long)0x80040210
#define DIERR_NOTACQUIRED (long)0x80040211
#define DIERR_NOTBUFFERED (long)0x80040212
#define DIERR_NOTDOWNLOADED (long)0x80040213
#define DIERR_NOTEXCLUSIVEACQUIRED (long)0x80040214
#define DIERR_NOTFOUND (long)0x80040215
#define DIERR_NOTINITIALIZED (long)0x80040216
#define DIERR_OBJECTNOTFOUND (long)0x80040217
#define DIERR_OLDDIRECTINPUTVERSION (long)0x80040218
#define DIERR_OTHERAPPHASPRIO (long)0x80040219
#define DIERR_OUTOFMEMORY (long)0x8004021A
#define DIERR_READONLY (long)0x8004021B
#define DIERR_REPORTFULL (long)0x8004021C
#define DIERR_UNPLUGGED (long)0x8004021D
#define DIERR_UNSUPPORTED (long)0x8004021E

// ============================================================================
// DirectInput Structures
// ============================================================================

struct DIPROPHEADER {
  unsigned int dwSize;
  unsigned int dwHeaderSize;
  unsigned int dwObj;
  unsigned int dwHow;
};

struct DIPROPDWORD {
  DIPROPHEADER diph;
  unsigned int dwData;
};

// DIDEVICEOBJECTDATA — returned by IDirectInputDevice8::GetDeviceData
// On real Windows DirectInput this carries one buffered device event.
// Also defined in windows_base.h with an #ifndef guard; avoid redefinition.
#ifndef _DIDEVICEOBJECTDATA_DEFINED
#define _DIDEVICEOBJECTDATA_DEFINED
struct DIDEVICEOBJECTDATA {
  unsigned int dwOfs;       // Offset (scancode for keyboard)
  unsigned int dwData;      // Data (0x80 = pressed, 0x00 = released)
  unsigned int dwTimeStamp; // Timestamp in milliseconds
  unsigned int dwSequence;  // Sequence number
  unsigned long *dwAppData; // Application data (unused)
};
#endif

// ============================================================================
// SDL2 Keyboard Event Buffer (filled by EmscriptenInput.cpp)
//
// The ring buffer uses power-of-2 masking: index & (SIZE - 1).
// EmscriptenInput_PumpEvents() writes, GetDeviceData reads.
// ============================================================================

#define EMSCRIPTEN_DINPUT_BUFFER_SIZE 256

struct EmscriptenDIKEntry {
  unsigned int dwOfs;
  unsigned int dwData;
  unsigned int dwTimeStamp;
};

// Defined in EmscriptenInput.cpp (linked into z_generals)
// These are extern so the header-only GetDeviceData can read them.
extern EmscriptenDIKEntry g_emscriptenKeyBuffer[EMSCRIPTEN_DINPUT_BUFFER_SIZE];
extern int g_emscriptenKeyReadIdx;
extern int g_emscriptenKeyWriteIdx;

// ============================================================================
// DirectInput COM Interfaces (Emscripten stubs)
// ============================================================================

struct IDirectInputDevice8;
typedef IDirectInputDevice8 *LPDIRECTINPUTDEVICE8;

struct IDirectInput8 {
  unsigned int CreateDevice(int, LPDIRECTINPUTDEVICE8 *, void *);
  unsigned int SetDataFormat(void *) { return 0; }
  unsigned int Release() { return 0; }
};
typedef IDirectInput8 *LPDIRECTINPUT8;

struct IDirectInputDevice8 {
  unsigned int SetCooperativeLevel(void *, unsigned int) { return 0; }
  unsigned int Acquire() { return 0; } // Always succeeds
  unsigned int Unacquire() { return 0; }
  unsigned int GetDeviceState(unsigned int, void *) { return 0; }

  // GetDeviceData — reads one key event from the SDL-filled ring buffer.
  // This is called by DirectInputKeyboard::getKey() with num=1.
  unsigned int GetDeviceData(unsigned int cbObjData, void *rgdod,
                             unsigned long *pdwInOut, unsigned int dwFlags) {
    if (!rgdod || !pdwInOut || *pdwInOut == 0) {
      if (pdwInOut)
        *pdwInOut = 0;
      return 0; // DI_OK
    }

    // Check if there are events in the buffer
    if (g_emscriptenKeyReadIdx != g_emscriptenKeyWriteIdx) {
      int idx = g_emscriptenKeyReadIdx &
                (EMSCRIPTEN_DINPUT_BUFFER_SIZE - 1);
      DIDEVICEOBJECTDATA *out = (DIDEVICEOBJECTDATA *)rgdod;
      out->dwOfs = g_emscriptenKeyBuffer[idx].dwOfs;
      out->dwData = g_emscriptenKeyBuffer[idx].dwData;
      out->dwTimeStamp = g_emscriptenKeyBuffer[idx].dwTimeStamp;
      out->dwSequence = 0;
      out->dwAppData = nullptr;
      g_emscriptenKeyReadIdx++;
      *pdwInOut = 1;
    } else {
      *pdwInOut = 0;
    }
    return 0; // DI_OK
  }

  unsigned int SetEventNotification(void *) { return 0; }
  unsigned int SetProperty(const void *, void *) { return 0; }
  unsigned int SetDataFormat(const void *) { return 0; }
  unsigned int Release() { return 0; }
};

#define IID_IDirectInput8 1
#define GUID_SysKeyboard 2
extern const void *c_dfDIKeyboard;
#define DISCL_FOREGROUND 1
#define DISCL_NONEXCLUSIVE 2
#define DIPH_DEVICE 0
#define DIPROP_BUFFERSIZE ((const void *)1)
#define DI_OK 0L
#define S_FALSE 1L

#ifndef FAILED
#define FAILED(hr) (((int)(hr)) < 0)
#endif
#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((int)(hr)) >= 0)
#endif

inline unsigned int DirectInput8Create(void *hinst, unsigned int dwVersion,
                                       int riidltf, void **ppvOut,
                                       void *punkOuter) {
  if (ppvOut)
    *ppvOut = new IDirectInput8();
  return 0; // S_OK
}

// IDirectInput8::CreateDevice — allocate a device stub that reads from
// the SDL event buffer
inline unsigned int IDirectInput8::CreateDevice(int guid,
                                                LPDIRECTINPUTDEVICE8 *ppDevice,
                                                void *) {
  if (ppDevice)
    *ppDevice = new IDirectInputDevice8();
  return 0; // S_OK
}

#endif // __DINPUT_H__
