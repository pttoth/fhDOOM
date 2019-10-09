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

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "qe3.h"
#include "io.h"
#include "../../renderer/tr_local.h"

idList<eclass_t*> eclasses;

eclass_t::eclass_t(const char* name, idVec3 color)
	: name(name)
	, color(color) {
	fixedsize = false;
	mins.Zero();
	maxs.Zero();
	color.Zero();
	memset(&texdef, 0, sizeof(texdef));
	modelHandle = NULL;
	entityModel = NULL;
	nShowFlags = 0;
}

namespace {

struct evarPrefix_t {
	int type;
	const char *prefix;
};

static const evarPrefix_t EvarPrefixes[] = {
	{ EVAR_STRING,  "editor_var " },
	{ EVAR_INT,		"editor_int " },
	{ EVAR_FLOAT,	"editor_float " },
	{ EVAR_BOOL,	"editor_bool " },
	{ EVAR_COLOR,	"editor_color " },
	{ EVAR_MATERIAL,"editor_mat " },
	{ EVAR_MODEL,	"editor_model " },
	{ EVAR_GUI,		"editor_gui " },
	{ EVAR_SOUND,	"editor_snd "}
};

static const int NumEvarPrefixes = sizeof(EvarPrefixes) / sizeof(evarPrefix_t);
static eclass_t	*eclass_bad = NULL;

/*

the classname, color triple, and bounding box are parsed out of comments
A ? size means take the exact brush size.

/*QUAKED <classname> (0 0 0) ?
/*QUAKED <classname> (0 0 0) (-8 -8 -8) (8 8 8)

Flag names can follow the size description:

/*QUAKED func_door (0 .5 .8) ? START_OPEN STONE_SOUND DOOR_DONT_LINK GOLD_KEY SILVER_KEY

*/



eclass_t *EClass_InitFromDict( const idDict *d, const char *name ) {
	const idKeyValue	*kv;

	// only include entityDefs with "editor_" values in them
	if ( !d->MatchPrefix( "editor_" ) ) {
		return NULL;
	}

	auto e = new eclass_t(name, d->GetVector("editor_color", "0 0 1"));

	e->defArgs = *d;

	idStr str;
	idStr text;
	idStr varname;
	idStr defaultStr;

	d->GetString("editor_mins", "", str);
	if (str != "?") {
		d->GetVector("editor_mins", "0 0 0", e->mins);
		d->GetVector("editor_maxs", "0 0 0", e->maxs);
		e->fixedsize = true;
	} else {
		e->fixedsize = false;
	}


	d->GetString("editor_material", "", e->defMaterial);

	//str = d->GetString("model");
	//if (str.Length()) {
	//	e->entityModel = renderModelManager->FindModel(str);
	//}

	str = "";

	// concatenate all editor usage comments
	text = "";
	kv = d->MatchPrefix( "editor_usage" );
	while( kv != NULL ) {
		text += kv->GetValue();
		if ( !kv->GetValue().Length() || ( text[ text.Length() - 1 ] != '\n' ) ) {
			text += "\n";
		}
		kv = d->MatchPrefix( "editor_usage", kv );
	}

	e->desc = text;

	str += "Spawn args:\n";
	for (int i = 0; i < NumEvarPrefixes; i++) {
		kv = d->MatchPrefix(EvarPrefixes[i].prefix);
		while (kv) {
			evar_t ev;
			kv->GetKey().Right( kv->GetKey().Length() - strlen(EvarPrefixes[i].prefix), ev.name );
			ev.desc = kv->GetValue();
			ev.type = EvarPrefixes[i].type;
			e->vars.Append(ev);
			kv = d->MatchPrefix(EvarPrefixes[i].prefix, kv);
		}
	}

/*
	while( kv != NULL ) {
		kv->key.Right( kv->key.Length() - 11, varname );
		str += va( "'%s':\t %s", varname.c_str(), kv->value.c_str() );
		if ( d->GetString( varname, "", defaultStr ) && defaultStr.Length() ) {
			str += va( "  Default '%s'.", defaultStr.c_str() );
		}
		str += "\n";
		kv = d->MatchPrefix( "editor_var ", kv );
	}

	e->comments = Mem_CopyString( str.c_str() );
*/


	// concatenate all variable comments
	kv = d->MatchPrefix( "editor_copy" );
	while (kv) {
		const char *temp = d->GetString(kv->GetValue());
		if (temp && *temp) {
			e->args.Set(kv->GetValue(), d->GetString(kv->GetValue()));
		}
		kv = d->MatchPrefix("editor_copy", kv);
	}

	// setup show flags
	e->nShowFlags = 0;
	if (d->GetBool("editor_rotatable")) {
		e->nShowFlags |= ECLASS_ROTATABLE;
	}

	if (d->GetBool("editor_showangle")) {
		e->nShowFlags |= ECLASS_ANGLE;
	}

	if (d->GetBool("editor_mover")) {
		e->nShowFlags |= ECLASS_MOVER;
	}

	if (d->GetBool("editor_env") || idStr::Icmpn(e->name, "env_", 4) == 0) {
		e->nShowFlags |= (ECLASS_ENV | ECLASS_ROTATABLE);
		if (d->GetBool("editor_ragdoll")) {
			e->defArgs.Set("model", "");
		}
	}

	if (d->GetBool("editor_combatnode")) {
		e->nShowFlags |= ECLASS_COMBATNODE;
	}

	if (d->GetBool("editor_light")) {
		e->nShowFlags |= ECLASS_LIGHT;
	}

	if ( idStr::Icmp(e->name, "light") == 0 ) {
		e->nShowFlags |= ECLASS_LIGHT;
	} else if ( idStr::Icmp(e->name, "path") == 0 ) {
		e->nShowFlags |= ECLASS_PATH;
	} else if ( idStr::Icmp(e->name, "target_null") == 0 ) {
		e->nShowFlags |= ECLASS_CAMERAVIEW;
	} else if ( idStr::Icmp(e->name, "worldspawn") == 0 ) {
		e->nShowFlags |= ECLASS_WORLDSPAWN;
	} else if ( idStr::Icmp(e->name, "speaker") == 0 ) {
		e->nShowFlags |= ECLASS_SPEAKER;
	} else if ( idStr::Icmp( e->name, "func_emitter" ) == 0 || idStr::Icmp( e->name, "func_splat" ) == 0 ) {
		e->nShowFlags |= ECLASS_PARTICLE;
	} else if ( idStr::Icmp(e->name, "func_liquid") == 0 ) {
		e->nShowFlags |= ECLASS_LIQUID;
	}

	return e;
}

static int cmp_eclass(eclass_t* const* _a, eclass_t* const* _b) {
	const eclass_t* a = *_a;
	const eclass_t* b = *_b;
	assert(a);
	assert(b);
	return stricmp(a->name, b->name);
}

}

void Eclass_Cleanup()
{
	eclasses.DeleteContents(true);

	delete eclass_bad;
	eclass_bad = NULL;
}

void Eclass_InitForSourceDirectory()
{
	int c = declManager->GetNumDecls(DECL_ENTITYDEF);
	for (int i = 0; i < c; i++) {
		const idDeclEntityDef *def = static_cast<const idDeclEntityDef *>( declManager->DeclByIndex( DECL_ENTITYDEF, i ) );
		if ( def ) {
			eclass_t *e = EClass_InitFromDict( &def->dict, def->GetName() );
			if ( e ) {
				eclasses.Append(e);
			}
		}
	}

	eclasses.Sort(cmp_eclass);

	eclass_bad = new eclass_t("UKNOWN ENTITY CLASS", idVec3(0, 0.5f, 0));
	if ( !eclass_bad ) {
		return;
	}
	eclass_bad->fixedsize = false;
}

eclass_t *Eclass_ForName (const char *name, bool has_brushes)
{
	if (!name) {
		return eclass_bad;
	}

	for (auto e : eclasses) {
		if (!strcmp(name, e->name)) {
			return e;
		}
	}

	char buff[1024];

	auto e = new eclass_t(name, idVec3(0, 0.5f, 0));

	sprintf(buff, "%s not found in def/*.def\n", name);
	e->comments = Mem_CopyString( buff );
	e->fixedsize = !has_brushes;
	e->mins.x = e->mins.y = e->mins.z = -8.0f;
	e->maxs.x = e->maxs.y = e->maxs.z = 8.0f;

	eclasses.Append(e);
	eclasses.Sort(cmp_eclass);

	return e;
}
