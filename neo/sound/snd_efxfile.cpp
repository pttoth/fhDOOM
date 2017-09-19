/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#include "../idlib/precompiled.h"
#pragma hdrstop

#include "snd_local.h"

/*
===============
idSoundEffect::idSoundEffect
===============
*/
idSoundEffect::idSoundEffect()
	: alEffect(~0)
{
};

/*
===============
idSoundEffect::~idSoundEffect
===============
*/
idSoundEffect::~idSoundEffect() {
	Purge();
}

/*
===============
idSoundEffect::Purge
===============
*/
void idSoundEffect::Purge() {
	if (soundSystemLocal.EAXAvailable && alEffect != 0 && alEffect != ~0) {
		alDeleteEffects(1, &alEffect);
	}

	alEffect = ~0;
}

/*
===============
idSoundEffect::Init
===============
*/
void idSoundEffect::Init() {
	Purge();

	if (!soundSystemLocal.EAXAvailable) {
		alEffect = 0;
		return;
	}

	alGenEffects(1, &alEffect);
	if (alEffect == 0) {
		return;
	}

	alEffecti(alEffect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);

	auto MillibelToGain = [](ALfloat millibels, ALfloat min, ALfloat max) {
		return idMath::ClampFloat(min, max, idMath::Pow(10.0f, millibels / 2000.0f));
	};

	SetProperty(AL_EAXREVERB_DENSITY, eax.flEnvironmentSize < 2.0f ? eax.flEnvironmentSize - 1.0f : 1.0f);
	SetProperty(AL_EAXREVERB_DIFFUSION, eax.flEnvironmentDiffusion);
	SetProperty(AL_EAXREVERB_GAIN, MillibelToGain(eax.lRoom, AL_EAXREVERB_MIN_GAIN, AL_EAXREVERB_MAX_GAIN));
	SetProperty(AL_EAXREVERB_GAINHF, MillibelToGain(eax.lRoomHF, AL_EAXREVERB_MIN_GAINHF, AL_EAXREVERB_MAX_GAINHF));
	SetProperty(AL_EAXREVERB_GAINLF, MillibelToGain(eax.lRoomLF, AL_EAXREVERB_MIN_GAINLF, AL_EAXREVERB_MAX_GAINLF));
	SetProperty(AL_EAXREVERB_DECAY_TIME, eax.flDecayTime);
	SetProperty(AL_EAXREVERB_DECAY_HFRATIO, eax.flDecayHFRatio);
	SetProperty(AL_EAXREVERB_DECAY_LFRATIO, eax.flDecayLFRatio);
	SetProperty(AL_EAXREVERB_REFLECTIONS_GAIN, MillibelToGain(eax.lReflections, AL_EAXREVERB_MIN_REFLECTIONS_GAIN, AL_EAXREVERB_MAX_REFLECTIONS_GAIN));
	SetProperty(AL_EAXREVERB_REFLECTIONS_DELAY, eax.flReflectionsDelay);
	SetProperty(AL_EAXREVERB_REFLECTIONS_PAN, eax.vReflectionsPan);
	SetProperty(AL_EAXREVERB_LATE_REVERB_GAIN, MillibelToGain(eax.lReverb, AL_EAXREVERB_MIN_LATE_REVERB_GAIN, AL_EAXREVERB_MAX_LATE_REVERB_GAIN));
	SetProperty(AL_EAXREVERB_LATE_REVERB_DELAY, eax.flReverbDelay);
	SetProperty(AL_EAXREVERB_LATE_REVERB_PAN, eax.vReflectionsPan);
	SetProperty(AL_EAXREVERB_ECHO_TIME, eax.flEchoTime);
	SetProperty(AL_EAXREVERB_ECHO_DEPTH, eax.flEchoDepth);
	SetProperty(AL_EAXREVERB_MODULATION_TIME, eax.flModulationTime);
	SetProperty(AL_EAXREVERB_MODULATION_DEPTH, eax.flModulationDepth);
	SetProperty(AL_EAXREVERB_AIR_ABSORPTION_GAINHF, MillibelToGain(eax.flAirAbsorptionHF, AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF, AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF));
	SetProperty(AL_EAXREVERB_HFREFERENCE, eax.flHFReference);
	SetProperty(AL_EAXREVERB_LFREFERENCE, eax.flLFReference);
	SetProperty(AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, eax.flRoomRolloffFactor);
	SetProperty(AL_EAXREVERB_DECAY_HFLIMIT, (eax.ulFlags & 0x20) ? AL_TRUE : AL_FALSE);
}

/*
===============
idSoundEffect::BindEffect
===============
*/
void idSoundEffect::BindEffect(ALuint alEffectSlot) {
	assert(soundSystemLocal.EAXAvailable);

	if (alEffect == ~0) {
		Init();
	}

	if (alEffect == 0) {
		return;
	}

	alAuxiliaryEffectSloti(alEffectSlot, AL_EFFECTSLOT_EFFECT, alEffect);
}

/*
===============
idSoundEffect::SetProperty
===============
*/
void idSoundEffect::SetProperty(ALenum name, float value) {
	alEffectf(alEffect, name, value);
};

/*
===============
idSoundEffect::SetProperty
===============
*/
void idSoundEffect::SetProperty(ALenum name, const EAXVECTOR& value) {
	alEffectfv(alEffect, name, &value.x);
};

/*
===============
idSoundEffect::SetProperty
===============
*/
void idSoundEffect::SetProperty(ALenum name, int value) {
	alEffecti(alEffect, name, value);
};



/*
===============
idEFXFile::idEFXFile
===============
*/
idEFXFile::idEFXFile( void ) { }

/*
===============
idEFXFile::Clear
===============
*/
void idEFXFile::Clear( void ) {
	effects.DeleteContents( true );
}

/*
===============
idEFXFile::~idEFXFile
===============
*/
idEFXFile::~idEFXFile( void ) {
	Clear();
}

/*
===============
idEFXFile::FindEffect
===============
*/
idSoundEffect* idEFXFile::FindEffect(const idStr& name) {
	for (int i = 0; i < effects.Num(); i++) {
		if (effects[i] && effects[i]->name == name) {
			return effects[i];
		}
	}
	return nullptr;
}

/*
===============
idEFXFile::ReadEffect
===============
*/
idSoundEffect* idEFXFile::ReadEffect(idLexer &src) {
	idToken token;

	if ( !src.ReadToken( &token ) )
		return nullptr;

	// reverb effect
	if (token != "reverb") {
		// other effect (not supported at the moment)
		src.Error("idEFXFile::ReadEffect: Unknown effect definition: %s", token.c_str());
		return nullptr;
	}

	EAXREVERBPROPERTIES reverb;

	src.ReadTokenOnLine( &token );
	const idToken name = token;

	if ( !src.ReadToken( &token ) ) {
		return nullptr;
	}

	if ( token != "{" ) {
		src.Error( "idEFXFile::ReadEffect: { not found, found %s", token.c_str() );
		return nullptr;
	}

	while (true) {
		if ( !src.ReadToken( &token ) ) {
			src.Error( "idEFXFile::ReadEffect: EOF without closing brace" );
			return nullptr;
		}

		if ( token == "}" ) {
			auto effect = new idSoundEffect();
			effect->name = name;
			effect->eax = reverb;
			return effect;
		}

		if ( token == "environment" ) {
			src.ReadTokenOnLine( &token );
			reverb.ulEnvironment = token.GetUnsignedLongValue();
		} else if ( token == "environment size" ) {
			reverb.flEnvironmentSize = src.ParseFloat();
		} else if ( token == "environment diffusion" ) {
			reverb.flEnvironmentDiffusion = src.ParseFloat();
		} else if ( token == "room" ) {
			reverb.lRoom = src.ParseInt();
		} else if ( token == "room hf" ) {
			reverb.lRoomHF = src.ParseInt();
		} else if ( token == "room lf" ) {
			reverb.lRoomLF = src.ParseInt();
		} else if ( token == "decay time" ) {
			reverb.flDecayTime = src.ParseFloat();
		} else if ( token == "decay hf ratio" ) {
			reverb.flDecayHFRatio = src.ParseFloat();
		} else if ( token == "decay lf ratio" ) {
			reverb.flDecayLFRatio = src.ParseFloat();
		} else if ( token == "reflections" ) {
			reverb.lReflections = src.ParseInt();
		} else if ( token == "reflections delay" ) {
			reverb.flReflectionsDelay = src.ParseFloat();
		} else if ( token == "reflections pan" ) {
			reverb.vReflectionsPan.x = src.ParseFloat();
			reverb.vReflectionsPan.y = src.ParseFloat();
			reverb.vReflectionsPan.z = src.ParseFloat();
		} else if ( token == "reverb" ) {
			reverb.lReverb = src.ParseInt();
		} else if ( token == "reverb delay" ) {
			reverb.flReverbDelay = src.ParseFloat();
		} else if ( token == "reverb pan" ) {
			reverb.vReverbPan.x = src.ParseFloat();
			reverb.vReverbPan.y = src.ParseFloat();
			reverb.vReverbPan.z = src.ParseFloat();
		} else if ( token == "echo time" ) {
			reverb.flEchoTime = src.ParseFloat();
		} else if ( token == "echo depth" ) {
			reverb.flEchoDepth = src.ParseFloat();
		} else if ( token == "modulation time" ) {
			reverb.flModulationTime = src.ParseFloat();
		} else if ( token == "modulation depth" ) {
			reverb.flModulationDepth = src.ParseFloat();
		} else if ( token == "air absorption hf" ) {
			reverb.flAirAbsorptionHF = src.ParseFloat();
		} else if ( token == "hf reference" ) {
			reverb.flHFReference = src.ParseFloat();
		} else if ( token == "lf reference" ) {
			reverb.flLFReference = src.ParseFloat();
		} else if ( token == "room rolloff factor" ) {
			reverb.flRoomRolloffFactor = src.ParseFloat();
		} else if ( token == "flags" ) {
			src.ReadTokenOnLine( &token );
			reverb.ulFlags = token.GetUnsignedLongValue();
		} else {
			src.ReadTokenOnLine( &token );
			src.Error( "idEFXFile::ReadEffect: Invalid parameter in reverb definition" );
			break;
		}
	}

	return nullptr;
}


/*
===============
idEFXFile::LoadFile
===============
*/
bool idEFXFile::LoadFile( const char *filename, bool OSPath ) {
	idLexer src( LEXFL_NOSTRINGCONCAT );
	idToken token;

	src.LoadFile( filename, OSPath );
	if ( !src.IsLoaded() ) {
		return false;
	}

	if ( !src.ExpectTokenString( "Version" ) ) {
		return false;
	}

	if ( src.ParseInt() != 1 ) {
		src.Error( "idEFXFile::LoadFile: Unknown file version" );
		return false;
	}

	while ( !src.EndOfFile() ) {
		idSoundEffect* effect = ReadEffect(src);
		if (effect) {
			effects.Append( effect );
		}
	};

	return true;
}


/*
===============
idEFXFile::UnloadFile
===============
*/
void idEFXFile::UnloadFile( void ) {
	Clear();
}
