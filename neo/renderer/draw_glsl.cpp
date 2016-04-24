#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_local.h"
#include "RenderProgram.h"
#include "ImmediateMode.h"
#include "RenderList.h"

idCVar r_pomEnabled("r_pomEnabled", "0", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_BOOL, "POM enabled or disabled");
idCVar r_pomMaxHeight("r_pomMaxHeight", "0.045", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_FLOAT, "maximum height for POM");
idCVar r_shading("r_shading", "0", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_INTEGER, "0 = Doom3 (Blinn-Phong), 1 = Phong");
idCVar r_specularExp("r_specularExp", "10", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_FLOAT, "exponent used for specularity");
idCVar r_specularScale("r_specularScale", "1", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_FLOAT, "scale specularity globally for all surfaces");
idCVar r_renderList("r_renderlist", "0", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_BOOL, "use render list");


using InteractionList = fhRenderList<drawInteraction_t>;

/*
=====================
RB_GLSL_BlendLight

=====================
*/
static void RB_GLSL_BlendLight(const drawSurf_t *surf) {
  const srfTriangles_t *tri;

  tri = surf->geo;

  fhRenderProgram::SetModelViewMatrix(GL_ModelViewMatrix.Top());
  fhRenderProgram::SetProjectionMatrix(GL_ProjectionMatrix.Top());

  if (backEnd.currentSpace != surf->space) {
    idPlane	lightProject[4];
    int		i;

    for (i = 0; i < 4; i++) {
      R_GlobalPlaneToLocal(surf->space->modelMatrix, backEnd.vLight->lightProject[i], lightProject[i]);
    }

	fhRenderProgram::SetBumpMatrix(lightProject[0].ToVec4(), lightProject[1].ToVec4());
	fhRenderProgram::SetSpecularMatrix(lightProject[2].ToVec4(), idVec4());
	fhRenderProgram::SetDiffuseMatrix(lightProject[3].ToVec4(), idVec4());
  }

  // this gets used for both blend lights and shadow draws
  if (tri->ambientCache) {
    int offset = vertexCache.Bind(tri->ambientCache);
	GL_SetupVertexAttributes(fhVertexLayout::DrawPosOnly, offset);
  }
  else if (tri->shadowCache) {
    int offset = vertexCache.Bind(tri->shadowCache);
	GL_SetupVertexAttributes(fhVertexLayout::Shadow, offset);
  }

  RB_DrawElementsWithCounters(tri);
}


/*
=====================
RB_GLSL_BlendLight

Dual texture together the falloff and projection texture with a blend
mode to the framebuffer, instead of interacting with the surface texture
=====================
*/
void RB_GLSL_BlendLight(const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2) {
  const idMaterial	*lightShader;
  const shaderStage_t	*stage;
  int					i;
  const float	*regs;

  if (!drawSurfs) {
    return;
  }
  if (r_skipBlendLights.GetBool()) {
    return;
  }
  RB_LogComment("---------- RB_GLSL_BlendLight ----------\n");

  GL_UseProgram(blendLightProgram);

  lightShader = backEnd.vLight->lightShader;
  regs = backEnd.vLight->shaderRegisters;

  // texture 1 will get the falloff texture
  backEnd.vLight->falloffImage->Bind(1);

  for (i = 0; i < lightShader->GetNumStages(); i++) {
    stage = lightShader->GetStage(i);

    if (!regs[stage->conditionRegister]) {
      continue;
    }

    GL_State(GLS_DEPTHMASK | stage->drawStateBits | GLS_DEPTHFUNC_EQUAL);

    // texture 0 will get the projected texture
    stage->texture.image->Bind(0);

    if (stage->texture.hasMatrix) {
      RB_LoadShaderTextureMatrix(regs, &stage->texture);
    }

    // get the modulate values from the light, including alpha, unlike normal lights
    backEnd.lightColor[0] = regs[stage->color.registers[0]];
    backEnd.lightColor[1] = regs[stage->color.registers[1]];
    backEnd.lightColor[2] = regs[stage->color.registers[2]];
    backEnd.lightColor[3] = regs[stage->color.registers[3]];
	fhRenderProgram::SetDiffuseColor(idVec4(backEnd.lightColor));    

    RB_RenderDrawSurfChainWithFunction(drawSurfs, RB_GLSL_BlendLight);
    RB_RenderDrawSurfChainWithFunction(drawSurfs2, RB_GLSL_BlendLight);

    if (stage->texture.hasMatrix) {
      GL_SelectTexture(0);
      GL_TextureMatrix.LoadIdentity();
    }
  }
}


/*
==================
R_WobbleskyTexGen
==================
*/
static void R_CreateWobbleskyTexMatrix(const drawSurf_t *surf, float time, float matrix[16]) {
  
  const int *parms = surf->material->GetTexGenRegisters();

  float	wobbleDegrees = surf->shaderRegisters[parms[0]];
  float	wobbleSpeed = surf->shaderRegisters[parms[1]];
  float	rotateSpeed = surf->shaderRegisters[parms[2]];

  wobbleDegrees = wobbleDegrees * idMath::PI / 180;
  wobbleSpeed = wobbleSpeed * 2 * idMath::PI / 60;
  rotateSpeed = rotateSpeed * 2 * idMath::PI / 60;

  // very ad-hoc "wobble" transform  
  float	a = time * wobbleSpeed;
  float	s = sin(a) * sin(wobbleDegrees);
  float	c = cos(a) * sin(wobbleDegrees);
  float	z = cos(wobbleDegrees);

  idVec3	axis[3];

  axis[2][0] = c;
  axis[2][1] = s;
  axis[2][2] = z;

  axis[1][0] = -sin(a * 2) * sin(wobbleDegrees);
  axis[1][2] = -s * sin(wobbleDegrees);
  axis[1][1] = sqrt(1.0f - (axis[1][0] * axis[1][0] + axis[1][2] * axis[1][2]));

  // make the second vector exactly perpendicular to the first
  axis[1] -= (axis[2] * axis[1]) * axis[2];
  axis[1].Normalize();

  // construct the third with a cross
  axis[0].Cross(axis[1], axis[2]);

  // add the rotate
  s = sin(rotateSpeed * time);
  c = cos(rotateSpeed * time);
  
  matrix[0] = axis[0][0] * c + axis[1][0] * s;
  matrix[4] = axis[0][1] * c + axis[1][1] * s;
  matrix[8] = axis[0][2] * c + axis[1][2] * s;

  matrix[1] = axis[1][0] * c - axis[0][0] * s;
  matrix[5] = axis[1][1] * c - axis[0][1] * s;
  matrix[9] = axis[1][2] * c - axis[0][2] * s;

  matrix[2] = axis[2][0];
  matrix[6] = axis[2][1];
  matrix[10] = axis[2][2];

  matrix[3] = matrix[7] = matrix[11] = 0.0f;
  matrix[12] = matrix[13] = matrix[14] = 0.0f;
}

/*
===============
RB_RenderTriangleSurface

Sets texcoord and vertex pointers
===============
*/
static void RB_GLSL_RenderTriangleSurface(const srfTriangles_t *tri) {
  if (!tri->ambientCache) {
    RB_DrawElementsImmediate(tri);
    return;
  }

  auto offset = vertexCache.Bind(tri->ambientCache);

  GL_SetupVertexAttributes(fhVertexLayout::DrawPosOnly, offset);  
  RB_DrawElementsWithCounters(tri);
}

static idPlane	fogPlanes[4];

/*
=====================
RB_T_BasicFog

=====================
*/
static void RB_GLSL_BasicFog(const drawSurf_t *surf) {
  fhRenderProgram::SetModelViewMatrix(GL_ModelViewMatrix.Top());
  fhRenderProgram::SetProjectionMatrix(GL_ProjectionMatrix.Top());

  if (backEnd.currentSpace != surf->space) {
    idPlane	local;

    GL_SelectTexture(0);

    R_GlobalPlaneToLocal(surf->space->modelMatrix, fogPlanes[0], local);
    local[3] += 0.5;
	const idVec4 bumpMatrixS = local.ToVec4();    

    local[0] = local[1] = local[2] = 0; local[3] = 0.5;

	const idVec4 bumpMatrixT = local.ToVec4();
    fhRenderProgram::SetBumpMatrix(bumpMatrixS, bumpMatrixT);

    GL_SelectTexture(1);

    // GL_S is constant per viewer
    R_GlobalPlaneToLocal(surf->space->modelMatrix, fogPlanes[2], local);
    local[3] += FOG_ENTER;
	const idVec4 diffuseMatrixT = local.ToVec4();   
	
    R_GlobalPlaneToLocal(surf->space->modelMatrix, fogPlanes[3], local);
	const idVec4 diffuseMatrixS = local.ToVec4();

	fhRenderProgram::SetDiffuseMatrix(diffuseMatrixS, diffuseMatrixT);
  }

  RB_GLSL_RenderTriangleSurface(surf->geo);
}

/*
==================
R_GLSL_Init

Load default shaders.
==================
*/
void	R_GLSL_Init( void )
{
	fhRenderProgram::Init();
	fhImmediateMode::Init();
}


/*
==================
RB_GLSL_FogPass
==================
*/
void RB_GLSL_FogPass(const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2) {
  assert(fogLightProgram);

  RB_LogComment("---------- RB_GLSL_FogPass ----------\n");  

  GL_UseProgram(fogLightProgram);

  // create a surface for the light frustom triangles, which are oriented drawn side out
  const srfTriangles_t* frustumTris = backEnd.vLight->frustumTris;

  // if we ran out of vertex cache memory, skip it
  if (!frustumTris->ambientCache) {
    return;
  }    
  
  drawSurf_t ds;
  memset(&ds, 0, sizeof(ds));
  ds.space = &backEnd.viewDef->worldSpace;
  ds.geo = frustumTris;
  ds.scissorRect = backEnd.viewDef->scissor;

  // find the current color and density of the fog
  const idMaterial *lightShader = backEnd.vLight->lightShader;
  const float	     *regs        = backEnd.vLight->shaderRegisters;
  // assume fog shaders have only a single stage
  const shaderStage_t	*stage = lightShader->GetStage(0);

  backEnd.lightColor[0] = regs[stage->color.registers[0]];
  backEnd.lightColor[1] = regs[stage->color.registers[1]];
  backEnd.lightColor[2] = regs[stage->color.registers[2]];
  backEnd.lightColor[3] = regs[stage->color.registers[3]];
  
  fhRenderProgram::SetDiffuseColor(idVec4(backEnd.lightColor));

  // calculate the falloff planes
  float	a;

  // if they left the default value on, set a fog distance of 500
  if (backEnd.lightColor[3] <= 1.0) {
    a = -0.5f / DEFAULT_FOG_DISTANCE;
  }
  else {
    // otherwise, distance = alpha color
    a = -0.5f / backEnd.lightColor[3];
  }  

  // texture 0 is the falloff image
  globalImages->fogImage->Bind(0);

  fogPlanes[0][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[2];
  fogPlanes[0][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[6];
  fogPlanes[0][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[10];
  fogPlanes[0][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[14];

  fogPlanes[1][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[0];
  fogPlanes[1][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[4];
  fogPlanes[1][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[8];
  fogPlanes[1][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[12];

  // texture 1 is the entering plane fade correction
  globalImages->fogEnterImage->Bind(1);

  // T will get a texgen for the fade plane, which is always the "top" plane on unrotated lights
  fogPlanes[2][0] = 0.001f * backEnd.vLight->fogPlane[0];
  fogPlanes[2][1] = 0.001f * backEnd.vLight->fogPlane[1];
  fogPlanes[2][2] = 0.001f * backEnd.vLight->fogPlane[2];
  fogPlanes[2][3] = 0.001f * backEnd.vLight->fogPlane[3];

  // S is based on the view origin
  float s = backEnd.viewDef->renderView.vieworg * fogPlanes[2].Normal() + fogPlanes[2][3];

  fogPlanes[3][0] = 0;
  fogPlanes[3][1] = 0;
  fogPlanes[3][2] = 0;
  fogPlanes[3][3] = FOG_ENTER + s;

  // draw it
  backEnd.glState.forceGlState = true;
  GL_State(GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL);
  RB_RenderDrawSurfChainWithFunction(drawSurfs, RB_GLSL_BasicFog);
  RB_RenderDrawSurfChainWithFunction(drawSurfs2, RB_GLSL_BasicFog);

  // the light frustum bounding planes aren't in the depth buffer, so use depthfunc_less instead
  // of depthfunc_equal
  GL_State(GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_LESS);
  GL_Cull(CT_BACK_SIDED);
  RB_RenderDrawSurfChainWithFunction(&ds, RB_GLSL_BasicFog);
  GL_Cull(CT_FRONT_SIDED);

  globalImages->BindNull(1);

  GL_SelectTexture(0);
  
  GL_UseProgram(nullptr);
}




/*
==============================================================================

BACK END RENDERING OF STENCIL SHADOWS

==============================================================================
*/


/*
=====================
RB_GLSL_Shadow

the shadow volumes face INSIDE
=====================
*/

static void RB_GLSL_Shadow(const drawSurf_t *surf) {
  const srfTriangles_t	*tri;

  // set the light position if we are using a vertex program to project the rear surfaces
  if (surf->space != backEnd.currentSpace) {
    idVec4 localLight;

    R_GlobalPointToLocal(surf->space->modelMatrix, backEnd.vLight->globalLightOrigin, localLight.ToVec3());
    localLight.w = 0.0f;

    assert(shadowProgram);
    fhRenderProgram::SetLocalLightOrigin(localLight);
    fhRenderProgram::SetProjectionMatrix(GL_ProjectionMatrix.Top());
    fhRenderProgram::SetModelViewMatrix(GL_ModelViewMatrix.Top());
  }

  tri = surf->geo;

  if (!tri->shadowCache) {
    return;
  }

  const auto offset = vertexCache.Bind(tri->shadowCache);

  GL_SetupVertexAttributes(fhVertexLayout::Shadow, offset);
  //GL_SetVertexLayout(fhVertexLayout::Shadow);  
  //glVertexAttribPointer(fhRenderProgram::vertex_attrib_position_shadow, 4, GL_FLOAT, false, sizeof(shadowCache_t), attributeOffset(offset, 0));

  // we always draw the sil planes, but we may not need to draw the front or rear caps
  int	numIndexes;
  bool external = false;

  if (!r_useExternalShadows.GetInteger()) {
    numIndexes = tri->numIndexes;
  }
  else if (r_useExternalShadows.GetInteger() == 2) { // force to no caps for testing
    numIndexes = tri->numShadowIndexesNoCaps;
  }
  else if (!(surf->dsFlags & DSF_VIEW_INSIDE_SHADOW)) {
    // if we aren't inside the shadow projection, no caps are ever needed needed
    numIndexes = tri->numShadowIndexesNoCaps;
    external = true;
  }
  else if (!backEnd.vLight->viewInsideLight && !(surf->geo->shadowCapPlaneBits & SHADOW_CAP_INFINITE)) {
    // if we are inside the shadow projection, but outside the light, and drawing
    // a non-infinite shadow, we can skip some caps
    if (backEnd.vLight->viewSeesShadowPlaneBits & surf->geo->shadowCapPlaneBits) {
      // we can see through a rear cap, so we need to draw it, but we can skip the
      // caps on the actual surface
      numIndexes = tri->numShadowIndexesNoFrontCaps;
    }
    else {
      // we don't need to draw any caps
      numIndexes = tri->numShadowIndexesNoCaps;
    }
    external = true;
  }
  else {
    // must draw everything
    numIndexes = tri->numIndexes;
  }

  // set depth bounds
  if (glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool()) {
    glDepthBoundsEXT(surf->scissorRect.zmin, surf->scissorRect.zmax);
  }
/*
  // debug visualization
  if (r_showShadows.GetInteger()) {

    if (r_showShadows.GetInteger() == 3) {
      if (external) {
        glColor3f(0.1 / backEnd.overBright, 1 / backEnd.overBright, 0.1 / backEnd.overBright);
      }
      else {
        // these are the surfaces that require the reverse
        glColor3f(1 / backEnd.overBright, 0.1 / backEnd.overBright, 0.1 / backEnd.overBright);
      }
    }
    else {
      // draw different color for turboshadows
      if (surf->geo->shadowCapPlaneBits & SHADOW_CAP_INFINITE) {
        if (numIndexes == tri->numIndexes) {
          glColor3f(1 / backEnd.overBright, 0.1 / backEnd.overBright, 0.1 / backEnd.overBright);
        }
        else {
          glColor3f(1 / backEnd.overBright, 0.4 / backEnd.overBright, 0.1 / backEnd.overBright);
        }
      }
      else {
        if (numIndexes == tri->numIndexes) {
          glColor3f(0.1 / backEnd.overBright, 1 / backEnd.overBright, 0.1 / backEnd.overBright);
        }
        else if (numIndexes == tri->numShadowIndexesNoFrontCaps) {
          glColor3f(0.1 / backEnd.overBright, 1 / backEnd.overBright, 0.6 / backEnd.overBright);
        }
        else {
          glColor3f(0.6 / backEnd.overBright, 1 / backEnd.overBright, 0.1 / backEnd.overBright);
        }
      }
    }

    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glDisable(GL_STENCIL_TEST);
    GL_Cull(CT_TWO_SIDED);
    RB_DrawShadowElementsWithCounters(tri, numIndexes);
    GL_Cull(CT_FRONT_SIDED);
    glEnable(GL_STENCIL_TEST);    
    return;
  }
*/
  // patent-free work around
  if (!external) {
    // "preload" the stencil buffer with the number of volumes
    // that get clipped by the near or far clip plane
    glStencilOp(GL_KEEP, GL_DECR_WRAP, GL_DECR_WRAP);
    GL_Cull(CT_FRONT_SIDED);
    RB_DrawShadowElementsWithCounters(tri, numIndexes);
    glStencilOp(GL_KEEP, GL_INCR_WRAP, GL_INCR_WRAP);
    GL_Cull(CT_BACK_SIDED);
    RB_DrawShadowElementsWithCounters(tri, numIndexes);
  }

  // traditional depth-pass stencil shadows
  glStencilOp(GL_KEEP, GL_KEEP, GL_INCR_WRAP);
  GL_Cull(CT_FRONT_SIDED);
  RB_DrawShadowElementsWithCounters(tri, numIndexes);

  glStencilOp(GL_KEEP, GL_KEEP, GL_DECR_WRAP);
  GL_Cull(CT_BACK_SIDED);
  RB_DrawShadowElementsWithCounters(tri, numIndexes);
}


/*
=====================
RB_GLSL_StencilShadowPass

Stencil test should already be enabled, and the stencil buffer should have
been set to 128 on any surfaces that might receive shadows
=====================
*/



void RB_GLSL_StencilShadowPass(const drawSurf_t *drawSurfs) {
  assert(shadowProgram);

  if (backEnd.vLight->lightDef->ShadowMode() != shadowMode_t::StencilShadow) {
    return;
  }

  if (!drawSurfs) {
    return;
  }

  //glDisable(GL_VERTEX_PROGRAM_ARB);
  //glDisable(GL_FRAGMENT_PROGRAM_ARB);
  GL_UseProgram(shadowProgram);

  RB_LogComment("---------- RB_StencilShadowPass ----------\n");

  globalImages->BindNull();

  // for visualizing the shadows
  if (r_showShadows.GetInteger()) {
    if (r_showShadows.GetInteger() == 2) {
      // draw filled in
      GL_State(GLS_DEPTHMASK | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_LESS);
    }
    else {
      // draw as lines, filling the depth buffer
      GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_POLYMODE_LINE | GLS_DEPTHFUNC_ALWAYS);
    }
  }
  else {
    // don't write to the color buffer, just the stencil buffer
    GL_State(GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS);
  }

  if (r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat()) {
    glPolygonOffset(r_shadowPolygonFactor.GetFloat(), -r_shadowPolygonOffset.GetFloat());
    glEnable(GL_POLYGON_OFFSET_FILL);
  }

  glStencilFunc(GL_ALWAYS, 1, 255);

  if (glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool()) {
    glEnable(GL_DEPTH_BOUNDS_TEST_EXT);
  }

  RB_RenderDrawSurfChainWithFunction(drawSurfs, RB_GLSL_Shadow);

  GL_Cull(CT_FRONT_SIDED);

  if (r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat()) {
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  if (glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool()) {
    glDisable(GL_DEPTH_BOUNDS_TEST_EXT);
  }

  glStencilFunc(GL_GEQUAL, 128, 255);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

  GL_UseProgram(nullptr);
}



/*
==================
RB_GLSL_FillDepthBuffer
==================
*/
void RB_GLSL_FillDepthBuffer(const drawSurf_t *surf) {
  int			stage;
  const idMaterial	*shader;
  const shaderStage_t *pStage;
  const float	*regs;
  float		color[4];
  const srfTriangles_t	*tri;

  tri = surf->geo;
  shader = surf->material;

  // update the clip plane if needed
  if (backEnd.viewDef->numClipPlanes && surf->space != backEnd.currentSpace) {
    idPlane	plane;
    R_GlobalPlaneToLocal(surf->space->modelMatrix, backEnd.viewDef->clipPlanes[0], plane);
    plane[3] += 0.5;	// the notch is in the middle
  }

  if (!shader->IsDrawn()) {
    return;
  }

  // some deforms may disable themselves by setting numIndexes = 0
  if (!tri->numIndexes) {
    return;
  }

  // translucent surfaces don't put anything in the depth buffer and don't
  // test against it, which makes them fail the mirror clip plane operation
  if (shader->Coverage() == MC_TRANSLUCENT) {
    return;
  }

  if (!tri->ambientCache) {
    common->Printf("RB_T_FillDepthBuffer: !tri->ambientCache\n");
    return;
  }

  // get the expressions for conditionals / color / texcoords
  regs = surf->shaderRegisters;

  // if all stages of a material have been conditioned off, don't do anything
  for (stage = 0; stage < shader->GetNumStages(); stage++) {
    pStage = shader->GetStage(stage);
    // check the stage enable condition
    if (regs[pStage->conditionRegister] != 0) {
      break;
    }
  }
  if (stage == shader->GetNumStages()) {
    return;
  }

  // set polygon offset if necessary
  if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset());
  }

  // subviews will just down-modulate the color buffer by overbright
  if (shader->GetSort() == SS_SUBVIEW) {
    GL_State(GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS);
    color[0] =
      color[1] =
      color[2] = (1.0 / backEnd.overBright);
    color[3] = 1;
  }
  else {
    // others just draw black
    color[0] = 0;
    color[1] = 0;
    color[2] = 0;
    color[3] = 1;
  }

  const auto offset = vertexCache.Bind(tri->ambientCache);
  GL_SetupVertexAttributes(fhVertexLayout::DrawPosTexOnly, offset);

  fhRenderProgram::SetModelViewMatrix(GL_ModelViewMatrix.Top());
  fhRenderProgram::SetProjectionMatrix(GL_ProjectionMatrix.Top());

  bool drawSolid = false;

  if (shader->Coverage() == MC_OPAQUE) {
    drawSolid = true;
  }

  // we may have multiple alpha tested stages
  if (shader->Coverage() == MC_PERFORATED) {
    // if the only alpha tested stages are condition register omitted,
    // draw a normal opaque surface
    bool	didDraw = false;

    // perforated surfaces may have multiple alpha tested stages
    for (stage = 0; stage < shader->GetNumStages(); stage++) {
      pStage = shader->GetStage(stage);

      if (!pStage->hasAlphaTest) {
        continue;
      }

      // check the stage enable condition
      if (regs[pStage->conditionRegister] == 0) {
        continue;
      }

      // if we at least tried to draw an alpha tested stage,
      // we won't draw the opaque surface
      didDraw = true;

      // set the alpha modulate
      color[3] = regs[pStage->color.registers[3]];

      // skip the entire stage if alpha would be black
      if (color[3] <= 0) {
        continue;
      }

      // bind the texture
      pStage->texture.image->Bind(0);      

	  fhRenderProgram::SetAlphaTestEnabled(true);
	  fhRenderProgram::SetAlphaTestThreshold(regs[pStage->alphaTestRegister]);      
      fhRenderProgram::SetDiffuseColor(idVec4(color));

      // set texture matrix and texGens      

      if (pStage->privatePolygonOffset && !surf->material->TestMaterialFlag(MF_POLYGONOFFSET)) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset);
      }

      if (pStage->texture.hasMatrix) {
        idVec4 textureMatrix[2];
        RB_GetShaderTextureMatrix(surf->shaderRegisters, &pStage->texture, textureMatrix);
		fhRenderProgram::SetDiffuseMatrix(textureMatrix[0],  textureMatrix[1]);
      }
      

      // draw it
      RB_DrawElementsWithCounters(tri);

      //RB_FinishStageTexturing(pStage, surf, ac);
      {
        if (pStage->privatePolygonOffset && !surf->material->TestMaterialFlag(MF_POLYGONOFFSET)) {
          glDisable(GL_POLYGON_OFFSET_FILL);
        }

		fhRenderProgram::SetDiffuseMatrix(idVec4::identityS, idVec4::identityT);
		fhRenderProgram::SetAlphaTestEnabled(false);
      }
    }
    //glDisable(GL_ALPHA_TEST);
    if (!didDraw) {
      drawSolid = true;
    }
  }

  // draw the entire surface solid
  if (drawSolid) {
    fhRenderProgram::SetDiffuseColor(idVec4(color));    
    globalImages->whiteImage->Bind(0);

    // draw it
    RB_DrawElementsWithCounters(tri);
  }


  // reset polygon offset
  if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // reset blending
  if (shader->GetSort() == SS_SUBVIEW) {
    GL_State(GLS_DEPTHFUNC_LESS);
  }

}

void RB_GLSL_SubmitFillDepthRenderList( const DepthRenderList& renderlist ) {
	assert( depthProgram );

	// if we are just doing 2D rendering, no need to fill the depth buffer
	if (!backEnd.viewDef->viewEntitys) {
		return;
	}

	RB_LogComment( "---------- RB_GLSL_SubmitFillDepthRenderList ----------\n" );

	// enable the second texture for mirror plane clipping if needed
	if (backEnd.viewDef->numClipPlanes) {
		globalImages->alphaNotchImage->Bind( 1 );
	}

	// decal surfaces may enable polygon offset
	glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() );

	GL_State( GLS_DEPTHFUNC_LESS );

	// Enable stencil test if we are going to be using it for shadows.
	// If we didn't do this, it would be legal behavior to get z fighting
	// from the ambient pass and the light passes.
	glEnable( GL_STENCIL_TEST );
	glStencilFunc( GL_ALWAYS, 1, 255 );

	GL_UseProgram( depthProgram );

	const viewEntity_t* currentSpace = nullptr;
	bool depthHackActive = false;
	bool alphatestEnabled = false;
	idScreenRect currentScissor;
	if (r_useScissor.GetBool()) {
		glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		currentScissor.x1 = 0;
		currentScissor.y1 = 0;
		currentScissor.x2 = glConfig.vidWidth;
		currentScissor.y2 = glConfig.vidHeight;
	}

	fhRenderProgram::SetProjectionMatrix(backEnd.viewDef->projectionMatrix);
	fhRenderProgram::SetAlphaTestEnabled(false);

	const int num = renderlist.Num();
	for(int i = 0; i < num; ++i) {
		const auto& drawdepth = renderlist[i];

		const auto offset = vertexCache.Bind( drawdepth.surf->geo->ambientCache );
		GL_SetupVertexAttributes( fhVertexLayout::Draw, offset );

		if (currentSpace != drawdepth.surf->space) {
			fhRenderProgram::SetModelMatrix( drawdepth.surf->space->modelMatrix );
			fhRenderProgram::SetModelViewMatrix( drawdepth.surf->space->modelViewMatrix );

			if (drawdepth.surf->space->modelDepthHack) {
				RB_EnterModelDepthHack( drawdepth.surf->space->modelDepthHack );
				fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );
				depthHackActive = true;
			}
			else if (drawdepth.surf->space->weaponDepthHack) {
				RB_EnterWeaponDepthHack();
				fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );
				depthHackActive = true;
			}
			else if (depthHackActive) {
				RB_LeaveDepthHack();
				fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );
				depthHackActive = false;
			}

			// change the scissor if needed
			if (r_useScissor.GetBool() && !currentScissor.Equals( drawdepth.surf->scissorRect )) {
				currentScissor = drawdepth.surf->scissorRect;
				glScissor( backEnd.viewDef->viewport.x1 + currentScissor.x1,
					backEnd.viewDef->viewport.y1 + currentScissor.y1,
					currentScissor.x2 + 1 - currentScissor.x1,
					currentScissor.y2 + 1 - currentScissor.y1 );
			}

			currentSpace = drawdepth.surf->space;
		}

		if(drawdepth.polygonOffset) {
			glEnable( GL_POLYGON_OFFSET_FILL );
			glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * drawdepth.polygonOffset );
		}

		if(drawdepth.isSubView) {
			GL_State(GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS);
		}

		if(drawdepth.texture) {
			fhRenderProgram::SetAlphaTestEnabled(true);
			fhRenderProgram::SetAlphaTestThreshold(drawdepth.alphaTestThreshold);		
			fhRenderProgram::SetDiffuseMatrix(drawdepth.textureMatrix[0], drawdepth.textureMatrix[1]);
			drawdepth.texture->Bind(0);
			alphatestEnabled = true;
		} else {
			if(alphatestEnabled) {
				globalImages->whiteImage->Bind(0);
				fhRenderProgram::SetAlphaTestEnabled(false);
				alphatestEnabled = false;;
			}
		}

		RB_DrawElementsWithCounters(drawdepth.surf->geo);

		if (drawdepth.polygonOffset) {
			glDisable( GL_POLYGON_OFFSET_FILL );
		}

		if (drawdepth.isSubView) {
			GL_State(GLS_DEPTHFUNC_LESS);
		}
	}

	if (r_useScissor.GetBool()) {
		glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		backEnd.currentScissor.x1 = 0;
		backEnd.currentScissor.y1 = 0;
		backEnd.currentScissor.x2 = glConfig.vidWidth;
		backEnd.currentScissor.y2 = glConfig.vidHeight;
	}
}

void RB_GLSL_CreateFillDepthRenderList(drawSurf_t **drawSurfs, int numDrawSurfs, DepthRenderList& renderlist ) {
	for (int i = 0; i < numDrawSurfs; i++) {
		drawDepth_t drawdepth;

		const drawSurf_t* surf = drawSurfs[i];		
		const srfTriangles_t* tri = surf->geo;
		const idMaterial* shader = surf->material;

		if (!shader->IsDrawn()) {
			continue;
		}

		// some deforms may disable themselves by setting numIndexes = 0
		if (!tri->numIndexes) {
			continue;
		}

		// translucent surfaces don't put anything in the depth buffer and don't
		// test against it, which makes them fail the mirror clip plane operation
		if (shader->Coverage() == MC_TRANSLUCENT) {
			continue;
		}

		if (!tri->ambientCache) {
			common->Printf( "RB_T_FillDepthBuffer: !tri->ambientCache\n" );
			continue;
		}

		// get the expressions for conditionals / color / texcoords
		const float	* regs = surf->shaderRegisters;

		// if all stages of a material have been conditioned off, don't do anything
		int stage = 0;
		for (; stage < shader->GetNumStages(); stage++) {
			const shaderStage_t* pStage = shader->GetStage( stage );
			// check the stage enable condition
			if (regs[pStage->conditionRegister] != 0) {
				break;
			}
		}
		if (stage == shader->GetNumStages()) {
			continue;
		}

		// set polygon offset if necessary
		if (shader->TestMaterialFlag( MF_POLYGONOFFSET )) {
			drawdepth.polygonOffset = shader->GetPolygonOffset();
		}
		else {
			drawdepth.polygonOffset = 0;
		}

		// subviews will just down-modulate the color buffer by overbright		
		if (shader->GetSort() == SS_SUBVIEW) {
			drawdepth.isSubView = true;
			drawdepth.color[0] = drawdepth.color[1] = drawdepth.color[2] = (1.0 / backEnd.overBright);
			drawdepth.color[3] = 1;
		}
		else {
			drawdepth.isSubView = false;
			drawdepth.color[0] = drawdepth.color[1] = drawdepth.color[2] = 0;
			drawdepth.color[3] = 1;
		}

		drawdepth.surf = surf;
		drawdepth.texture = nullptr;

		// we may have multiple alpha tested stages
		if (shader->Coverage() == MC_PERFORATED) {
			// if the only alpha tested stages are condition register omitted,
			// draw a normal opaque surface
			bool	didDraw = false;

			// perforated surfaces may have multiple alpha tested stages
			for (stage = 0; stage < shader->GetNumStages(); stage++) {
				const shaderStage_t* pStage = shader->GetStage( stage );

				if (!pStage->hasAlphaTest) {
					continue;
				}

				// check the stage enable condition
				if (regs[pStage->conditionRegister] == 0) {
					continue;
				}

				// set the alpha modulate
				drawdepth.color[3] = regs[pStage->color.registers[3]];

				// skip the entire stage if alpha would be black
				if (drawdepth.color[3] <= 0) {
					continue;
				}

				// bind the texture
				drawdepth.texture = pStage->texture.image;
				drawdepth.alphaTestThreshold = regs[pStage->alphaTestRegister];

				// set texture matrix and texGens      

				if (pStage->privatePolygonOffset && !surf->material->TestMaterialFlag( MF_POLYGONOFFSET )) {
					drawdepth.polygonOffset = pStage->privatePolygonOffset;
				}

				if (pStage->texture.hasMatrix) {
					RB_GetShaderTextureMatrix( surf->shaderRegisters, &pStage->texture, drawdepth.textureMatrix );
				}
				else {
					drawdepth.textureMatrix[0] = idVec4::identityS;
					drawdepth.textureMatrix[1] = idVec4::identityT;
				}
			}
		}

		renderlist.Append(drawdepth);
	}
}

/*
=====================
RB_GLSL_FillDepthBuffer

If we are rendering a subview with a near clip plane, use a second texture
to force the alpha test to fail when behind that clip plane
=====================
*/
void RB_GLSL_FillDepthBuffer(drawSurf_t **drawSurfs, int numDrawSurfs) {
	if (r_renderList.GetBool()) {
		DepthRenderList depthRenderList;
		RB_GLSL_CreateFillDepthRenderList( drawSurfs, numDrawSurfs, depthRenderList );
		RB_GLSL_SubmitFillDepthRenderList( depthRenderList );
		return;
	}

  assert(depthProgram);

  // if we are just doing 2D rendering, no need to fill the depth buffer
  if (!backEnd.viewDef->viewEntitys) {
    return;
  }

  RB_LogComment("---------- RB_GLSL_FillDepthBuffer ----------\n");

  // enable the second texture for mirror plane clipping if needed
  if (backEnd.viewDef->numClipPlanes) {
    globalImages->alphaNotchImage->Bind(1);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_TEXTURE_GEN_S);
    glTexCoord2f(1, 0.5);
  }

  // decal surfaces may enable polygon offset
  glPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat());

  GL_State(GLS_DEPTHFUNC_LESS);

  // Enable stencil test if we are going to be using it for shadows.
  // If we didn't do this, it would be legal behavior to get z fighting
  // from the ambient pass and the light passes.
  glEnable(GL_STENCIL_TEST);
  glStencilFunc(GL_ALWAYS, 1, 255);

  GL_UseProgram(depthProgram);  

  RB_RenderDrawSurfListWithFunction(drawSurfs, numDrawSurfs, RB_GLSL_FillDepthBuffer);
  
  GL_UseProgram(nullptr);

  if (backEnd.viewDef->numClipPlanes) {
    globalImages->BindNull(1);
    glDisable(GL_TEXTURE_GEN_S);
    GL_SelectTexture(0);
  }
}



/*
=====================
RB_GLSL_RenderSpecialShaderStage
=====================
*/
void RB_GLSL_RenderSpecialShaderStage(const float* regs, const shaderStage_t* pStage, glslShaderStage_t* glslStage, const srfTriangles_t	*tri) {  

  GL_State(pStage->drawStateBits);
  GL_UseProgram(glslStage->program);

  const auto offset = vertexCache.Bind(tri->ambientCache);  
  GL_SetupVertexAttributes(fhVertexLayout::Draw, offset);

  for (int i = 0; i < glslStage->numShaderParms; i++) {
    idVec4 parm;
    parm[0] = regs[glslStage->shaderParms[i][0]];
    parm[1] = regs[glslStage->shaderParms[i][1]];
    parm[2] = regs[glslStage->shaderParms[i][2]];
    parm[3] = regs[glslStage->shaderParms[i][3]];
	fhRenderProgram::SetShaderParm(i, parm);    
  }

//  glUniformMatrix4fv(glslProgramDef_t::uniform_modelMatrix, 1, false, surf->space->modelMatrix);
  fhRenderProgram::SetModelViewMatrix(GL_ModelViewMatrix.Top());
  fhRenderProgram::SetProjectionMatrix(GL_ProjectionMatrix.Top());

  // current render
  const int	w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
  const int	h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;

  fhRenderProgram::SetCurrentRenderSize(
	  idVec2(globalImages->currentRenderImage->uploadWidth, globalImages->currentRenderImage->uploadHeight),
	  idVec2(w, h));

  // set textures
  for (int i = 0; i < glslStage->numShaderMaps; i++) {
    if (glslStage->shaderMap[i]) {
      glslStage->shaderMap[i]->Bind(i);
    }
  }

  // draw it
  RB_DrawElementsWithCounters(tri);

  for (int i = 0; i < glslStage->numShaderMaps; i++) {
    if (glslStage->shaderMap[i]) {
      globalImages->BindNull(i);
    }
  }

  GL_UseProgram(nullptr);
  GL_SelectTexture(0);
}



/*
=================
R_ReloadGlslPrograms_f
=================
*/
void R_ReloadGlslPrograms_f( const idCmdArgs &args ) {
	fhRenderProgram::ReloadAll();
}


/*
====================
RB_GLSL_RenderShaderStage
====================
*/
void RB_GLSL_RenderShaderStage(const drawSurf_t *surf, const shaderStage_t* pStage) {
  assert(defaultProgram);
  assert(skyboxProgram);
  assert(bumpyEnvProgram);

  // set the color  
  float color[4];
  color[0] = surf->shaderRegisters[pStage->color.registers[0]];
  color[1] = surf->shaderRegisters[pStage->color.registers[1]];
  color[2] = surf->shaderRegisters[pStage->color.registers[2]];
  color[3] = surf->shaderRegisters[pStage->color.registers[3]];

  // skip the entire stage if an add would be black
  if ((pStage->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE)
    && color[0] <= 0 && color[1] <= 0 && color[2] <= 0) {
    return;
  }

  // skip the entire stage if a blend would be completely transparent
  if ((pStage->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA)
    && color[3] <= 0) {
    return;
  }
  
  const auto offset = vertexCache.Bind(surf->geo->ambientCache); 


  if (pStage->texture.texgen == TG_DIFFUSE_CUBE) {
    return;
  }
  else if (pStage->texture.texgen == TG_SKYBOX_CUBE || pStage->texture.texgen == TG_WOBBLESKY_CUBE) {
    GL_UseProgram(skyboxProgram);    

    idMat4 textureMatrix = mat4_identity;
    if (pStage->texture.texgen == TG_WOBBLESKY_CUBE) {
      R_CreateWobbleskyTexMatrix(surf, backEnd.viewDef->floatTime, textureMatrix.ToFloatPtr());
    }

    idVec4 localViewOrigin;
    R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3() );
    localViewOrigin[3] = 1.0f;
    fhRenderProgram::SetLocalViewOrigin(localViewOrigin);
	fhRenderProgram::SetTextureMatrix(textureMatrix.ToFloatPtr());    

	GL_SetupVertexAttributes(fhVertexLayout::DrawPosColorOnly, offset);
  }
  else if (pStage->texture.texgen == TG_SCREEN) {
    return;
  }
  else if (pStage->texture.texgen == TG_GLASSWARP) {
    return;
  }
  else if (pStage->texture.texgen == TG_REFLECT_CUBE) {

    GL_UseProgram(bumpyEnvProgram);

    idMat4 textureMatrix = mat4_identity;

    idVec4 localViewOrigin;
    R_GlobalPointToLocal(surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3());
    localViewOrigin[3] = 1.0f;
	fhRenderProgram::SetLocalViewOrigin(localViewOrigin);        
	fhRenderProgram::SetTextureMatrix(textureMatrix.ToFloatPtr());

	GL_SetupVertexAttributes(fhVertexLayout::Draw, offset);

    // set the texture matrix
    idVec4 textureMatrixST[2];
    textureMatrixST[0][0] = 1;
    textureMatrixST[0][1] = 0;
    textureMatrixST[0][2] = 0;
    textureMatrixST[0][3] = 0;
    textureMatrixST[1][0] = 0;
    textureMatrixST[1][1] = 1;
    textureMatrixST[1][2] = 0;
    textureMatrixST[1][3] = 0;

    // see if there is also a bump map specified
    if(const shaderStage_t *bumpStage = surf->material->GetBumpStage()) {
      RB_GetShaderTextureMatrix(surf->shaderRegisters, &bumpStage->texture, textureMatrixST);

      //void RB_GetShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture, idVec4 matrix[2] );
      bumpStage->texture.image->Bind(2);
    } else {
      globalImages->flatNormalMap->Bind(2);
    }

	fhRenderProgram::SetBumpMatrix(textureMatrixST[0], textureMatrixST[1]);
  }
  else {    

    //prefer depth blend settings from material. If material does not define a 
    // depth blend mode, look at the geometry for depth blend settings (usually
    // set by particle systems for soft particles)

    depthBlendMode_t depthBlendMode = pStage->depthBlendMode;
    float depthBlendRange = pStage->depthBlendRange;

    if(depthBlendMode == DBM_UNDEFINED) {
      depthBlendMode = surf->geo->depthBlendMode;
      depthBlendRange = surf->geo->depthBlendRange;
    }      
    
    if (depthBlendMode == DBM_AUTO) {
      if (pStage->drawStateBits & (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE))
        depthBlendMode = DBM_COLORALPHA_ZERO;
      else if (pStage->drawStateBits & (GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO))
        depthBlendMode = DBM_COLORALPHA_ONE;
      else
        depthBlendMode = DBM_OFF;
    }

    if(depthBlendMode != DBM_OFF && depthBlendRange > 0.0f) {
      GL_UseProgram(depthblendProgram);

      globalImages->currentDepthImage->Bind(2);

      fhRenderProgram::SetDepthBlendRange(depthBlendRange);
      fhRenderProgram::SetDepthBlendMode(static_cast<int>(depthBlendMode));      
	  fhRenderProgram::SetClipRange(backEnd.viewDef->viewFrustum.GetNearDistance(), backEnd.viewDef->viewFrustum.GetFarDistance());      
    }
    else {
      GL_UseProgram(defaultProgram);
    }

	GL_SetupVertexAttributes(fhVertexLayout::DrawPosColorTexOnly, offset);
  }

  // bind the texture
  if (pStage->texture.cinematic) {
	  if (r_skipDynamicTextures.GetBool()) {
		  globalImages->defaultImage->Bind(1);		  
	  }
	  else {
		  // offset time by shaderParm[7] (FIXME: make the time offset a parameter of the shader?)
		  // We make no attempt to optimize for multiple identical cinematics being in view, or
		  // for cinematics going at a lower framerate than the renderer.
		  cinData_t	cin = pStage->texture.cinematic->ImageForTime( (int)(1000 * (backEnd.viewDef->floatTime + backEnd.viewDef->renderView.shaderParms[11])) );

		  if (cin.image) {
			  globalImages->cinematicImage->UploadScratch( 1, cin.image, cin.imageWidth, cin.imageHeight );
		  }
		  else {
			  globalImages->blackImage->Bind(1);
		  }
	  }
  }
  else {
	  //FIXME: see why image is invalid
	  if (pStage->texture.image) {
		  pStage->texture.image->Bind(1);
	  }
  }
  
  // set the state
  GL_State(pStage->drawStateBits);

  switch (pStage->vertexColor) {
  case SVC_IGNORE:
	fhRenderProgram::SetColorModulate(idVec4::zero);
	fhRenderProgram::SetColorAdd(idVec4::one);
    break;
  case SVC_MODULATE:
	fhRenderProgram::SetColorModulate(idVec4::one);
	fhRenderProgram::SetColorAdd(idVec4::zero);
    break;
  case SVC_INVERSE_MODULATE:
	fhRenderProgram::SetColorModulate(idVec4::negOne);
	fhRenderProgram::SetColorAdd(idVec4::one);
    break;
  }  
  
  fhRenderProgram::SetModelMatrix(surf->space->modelMatrix);
  fhRenderProgram::SetModelViewMatrix(GL_ModelViewMatrix.Top());
  fhRenderProgram::SetProjectionMatrix(GL_ProjectionMatrix.Top());

  // current render
  const int	w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
  const int	h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
  fhRenderProgram::SetCurrentRenderSize(
	  idVec2(globalImages->currentRenderImage->uploadWidth, globalImages->currentRenderImage->uploadHeight), 
	  idVec2(w,h));  

  // set privatePolygonOffset if necessary
  if (pStage->privatePolygonOffset) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset);
  }

  if(pStage->texture.image) {
    // set the texture matrix
    idVec4 textureMatrix[2];
    textureMatrix[0][0] = 1;
    textureMatrix[0][1] = 0;
    textureMatrix[0][2] = 0;
    textureMatrix[0][3] = 0;
    textureMatrix[1][0] = 0;
    textureMatrix[1][1] = 1;
    textureMatrix[1][2] = 0;
    textureMatrix[1][3] = 0;
    if(pStage->texture.hasMatrix) {
      RB_GetShaderTextureMatrix(surf->shaderRegisters, &pStage->texture, textureMatrix);
	}

	fhRenderProgram::SetBumpMatrix(textureMatrix[0], textureMatrix[1]);  

    pStage->texture.image->Bind(1);    
  }

  fhRenderProgram::SetDiffuseColor(idVec4(color));

  // draw it
  RB_DrawElementsWithCounters(surf->geo);
  
  GL_UseProgram(nullptr);
  GL_SelectTexture(0);
}


/*
==================
RB_GLSL_DrawInteraction
==================
*/
void	RB_GLSL_DrawInteraction(const drawInteraction_t *din) {  

	fhRenderProgram::SetModelMatrix(din->surf->space->modelMatrix);
	fhRenderProgram::SetModelViewMatrix(GL_ModelViewMatrix.Top());
	fhRenderProgram::SetProjectionMatrix(GL_ProjectionMatrix.Top());
	fhRenderProgram::SetLocalLightOrigin(din->localLightOrigin);
	fhRenderProgram::SetLocalViewOrigin(din->localViewOrigin);  
	fhRenderProgram::SetLightProjectionMatrix( din->lightProjection[0], din->lightProjection[1], din->lightProjection[2] );
	fhRenderProgram::SetLightFallOff(din->lightProjection[3]);
	fhRenderProgram::SetBumpMatrix(din->bumpMatrix[0], din->bumpMatrix[1]);
	fhRenderProgram::SetDiffuseMatrix(din->diffuseMatrix[0], din->diffuseMatrix[1]);
	fhRenderProgram::SetSpecularMatrix(din->specularMatrix[0], din->specularMatrix[1]);


  switch (din->vertexColor) {
  case SVC_IGNORE:
	  fhRenderProgram::SetColorModulate( idVec4::zero );
	  fhRenderProgram::SetColorAdd( idVec4::one );
	  break;
  case SVC_MODULATE:
	  fhRenderProgram::SetColorModulate( idVec4::one );
	  fhRenderProgram::SetColorAdd( idVec4::zero );
	  break;
  case SVC_INVERSE_MODULATE:
	  fhRenderProgram::SetColorModulate( idVec4::negOne );
	  fhRenderProgram::SetColorAdd( idVec4::one );
	  break;
  }

  fhRenderProgram::SetDiffuseColor(din->diffuseColor);
  fhRenderProgram::SetSpecularColor(din->specularColor * r_specularScale.GetFloat());

  if( r_pomEnabled.GetBool() && din->specularImage->hasAlpha ) {
	  fhRenderProgram::SetPomMaxHeight(r_pomMaxHeight.GetFloat());
  } else {
	  fhRenderProgram::SetPomMaxHeight(-1);	  
  }  


  // texture 1 will be the per-surface bump map  
  din->bumpImage->Bind(1);

  // texture 2 will be the light falloff texture  
  din->lightFalloffImage->Bind(2);

  // texture 3 will be the light projection texture  
  din->lightImage->Bind(3);

  // texture 4 is the per-surface diffuse map  
  din->diffuseImage->Bind(4);

  // texture 5 is the per-surface specular map  
  din->specularImage->Bind(5);

  // draw it
  RB_DrawElementsWithCounters(din->surf->geo);
}

/*
=============
RB_GLSL_CreateDrawInteractions

=============
*/
void RB_GLSL_CreateDrawInteractions(const drawSurf_t *surf) {
	assert(interactionProgram);

	if (!surf) {
		return;
	}

	// perform setup here that will be constant for all interactions
	GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc);
  
	GL_UseProgram( interactionProgram );

	fhRenderProgram::SetShading( r_shading.GetInteger() );
	fhRenderProgram::SetSpecularExp( r_specularExp.GetFloat() );
	fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );

	if (backEnd.vLight->lightDef->ShadowMode() == shadowMode_t::ShadowMap) {
		const idVec4 globalLightOrigin = idVec4( backEnd.vLight->globalLightOrigin, 1 );
		fhRenderProgram::SetGlobalLightOrigin( globalLightOrigin );

		const float shadowBrightness = backEnd.vLight->lightDef->ShadowBrightness();
		const float shadowSoftness = backEnd.vLight->lightDef->ShadowSoftness();
		fhRenderProgram::SetShadowParams( idVec4( shadowSoftness, shadowBrightness, 0, 0 ) );

		if (backEnd.vLight->lightDef->parms.pointLight) {
			//point light
			fhRenderProgram::SetShadowMappingMode( 1 );
			fhRenderProgram::SetPointLightProjectionMatrices( &backEnd.shadowViewProjection[0][0] );
		}
		else {
			//projected light
			fhRenderProgram::SetShadowMappingMode( 2 );
			fhRenderProgram::SetSpotLightProjectionMatrix( backEnd.testProjectionMatrix );
		}
	}
	else {
		//no shadows
		fhRenderProgram::SetShadowMappingMode( 0 );
	}

	for (; surf; surf = surf->nextOnLight) {
		// perform setup here that will not change over multiple interaction passes

		// set the vertex pointers
		const auto offset = vertexCache.Bind(surf->geo->ambientCache);
		GL_SetupVertexAttributes(fhVertexLayout::Draw, offset);

		// this may cause RB_ARB2_DrawInteraction to be exacuted multiple
		// times with different colors and images if the surface or light have multiple layers
		RB_CreateSingleDrawInteractions(surf, RB_GLSL_DrawInteraction);
	}  

	globalImages->BindNull(5);
	globalImages->BindNull(4);
	globalImages->BindNull(3);
	globalImages->BindNull(2);
	globalImages->BindNull(1);

	GL_SelectTexture(0);

	GL_UseProgram(nullptr);
}



/*
=================
RB_SubmittInteraction
=================
*/
static void RB_SubmittInteraction( drawInteraction_t *din, InteractionList& interactionList ) {
	if (!din->bumpImage) {
		return;
	}

	if (!din->diffuseImage || r_skipDiffuse.GetBool()) {
		din->diffuseImage = globalImages->blackImage;
	}
	if (!din->specularImage || r_skipSpecular.GetBool() || din->ambientLight) {
		din->specularImage = globalImages->blackImage;
	}
	if (!din->bumpImage || r_skipBump.GetBool()) {
		din->bumpImage = globalImages->flatNormalMap;
	}

	// if we wouldn't draw anything, don't call the Draw function
	if (
		((din->diffuseColor[0] > 0 ||
		din->diffuseColor[1] > 0 ||
		din->diffuseColor[2] > 0) && din->diffuseImage != globalImages->blackImage)
		|| ((din->specularColor[0] > 0 ||
		din->specularColor[1] > 0 ||
		din->specularColor[2] > 0) && din->specularImage != globalImages->blackImage)) {

		interactionList.Append(*din);
	}
}


/*
=============
RB_GLSL_CreateDrawInteractions

=============
*/
void RB_GLSL_CreateDrawInteractions( const drawSurf_t *surf, InteractionList& interactionList ) {

	if (r_skipInteractions.GetBool()) {
		return;
	}

	for (; surf; surf = surf->nextOnLight) {
		const idMaterial	*surfaceShader = surf->material;
		const float			*surfaceRegs = surf->shaderRegisters;
		const viewLight_t	*vLight = backEnd.vLight;
		const idMaterial	*lightShader = vLight->lightShader;
		const float			*lightRegs = vLight->shaderRegisters;
		drawInteraction_t	inter;
		inter.hasBumpMatrix = inter.hasDiffuseMatrix = inter.hasSpecularMatrix = false;

		if (!surf->geo || !surf->geo->ambientCache) {
			continue;
		}

		inter.surf = surf;
		inter.lightFalloffImage = vLight->falloffImage;

		R_GlobalPointToLocal( surf->space->modelMatrix, vLight->globalLightOrigin, inter.localLightOrigin.ToVec3() );
		R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, inter.localViewOrigin.ToVec3() );
		inter.localLightOrigin[3] = 0;
		inter.localViewOrigin[3] = 1;
		inter.ambientLight = lightShader->IsAmbientLight();

		// the base projections may be modified by texture matrix on light stages
		idPlane lightProject[4];
		for (int i = 0; i < 4; i++) {
			R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[i], lightProject[i] );
		}

		for (int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++) {
			const shaderStage_t	*lightStage = lightShader->GetStage( lightStageNum );

			// ignore stages that fail the condition
			if (!lightRegs[lightStage->conditionRegister]) {
				continue;
			}

			inter.lightImage = lightStage->texture.image;

			memcpy( inter.lightProjection, lightProject, sizeof(inter.lightProjection) );
			// now multiply the texgen by the light texture matrix
			if (lightStage->texture.hasMatrix) {
				RB_GetShaderTextureMatrix( lightRegs, &lightStage->texture, backEnd.lightTextureMatrix );
				RB_BakeTextureMatrixIntoTexgen( reinterpret_cast<class idPlane *>(inter.lightProjection), backEnd.lightTextureMatrix );
			}

			inter.bumpImage = NULL;
			inter.specularImage = NULL;
			inter.diffuseImage = NULL;
			inter.diffuseColor[0] = inter.diffuseColor[1] = inter.diffuseColor[2] = inter.diffuseColor[3] = 0;
			inter.specularColor[0] = inter.specularColor[1] = inter.specularColor[2] = inter.specularColor[3] = 0;

			float lightColor[4];

			// backEnd.lightScale is calculated so that lightColor[] will never exceed
			// tr.backEndRendererMaxLight
			lightColor[0] = backEnd.lightScale * lightRegs[lightStage->color.registers[0]];
			lightColor[1] = backEnd.lightScale * lightRegs[lightStage->color.registers[1]];
			lightColor[2] = backEnd.lightScale * lightRegs[lightStage->color.registers[2]];
			lightColor[3] = lightRegs[lightStage->color.registers[3]];

			// go through the individual stages
			for (int surfaceStageNum = 0; surfaceStageNum < surfaceShader->GetNumStages(); surfaceStageNum++) {
				const shaderStage_t	*surfaceStage = surfaceShader->GetStage( surfaceStageNum );

				switch (surfaceStage->lighting) {
				case SL_AMBIENT: {
					// ignore ambient stages while drawing interactions
					break;
				}
				case SL_BUMP: {
					// ignore stage that fails the condition
					if (!surfaceRegs[surfaceStage->conditionRegister]) {
						break;
					}
					// draw any previous interaction
					RB_SubmittInteraction( &inter, interactionList );
					inter.diffuseImage = NULL;
					inter.specularImage = NULL;
					R_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.bumpImage, inter.bumpMatrix, NULL );
					inter.hasBumpMatrix = surfaceStage->texture.hasMatrix;
					break;
				}
				case SL_DIFFUSE: {
					// ignore stage that fails the condition
					if (!surfaceRegs[surfaceStage->conditionRegister]) {
						break;
					}
					if (inter.diffuseImage) {
						RB_SubmittInteraction( &inter, interactionList );
					}
					R_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.diffuseImage,
						inter.diffuseMatrix, inter.diffuseColor.ToFloatPtr() );
					inter.diffuseColor[0] *= lightColor[0];
					inter.diffuseColor[1] *= lightColor[1];
					inter.diffuseColor[2] *= lightColor[2];
					inter.diffuseColor[3] *= lightColor[3];
					inter.vertexColor = surfaceStage->vertexColor;
					inter.hasDiffuseMatrix = surfaceStage->texture.hasMatrix;
					break;
				}
				case SL_SPECULAR: {
					// ignore stage that fails the condition
					if (!surfaceRegs[surfaceStage->conditionRegister]) {
						break;
					}
					if (inter.specularImage) {
						RB_SubmittInteraction( &inter, interactionList );
					}
					R_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.specularImage,
						inter.specularMatrix, inter.specularColor.ToFloatPtr() );
					inter.specularColor[0] *= lightColor[0];
					inter.specularColor[1] *= lightColor[1];
					inter.specularColor[2] *= lightColor[2];
					inter.specularColor[3] *= lightColor[3];
					inter.specularColor *= r_specularScale.GetFloat();
					inter.vertexColor = surfaceStage->vertexColor;
					inter.hasSpecularMatrix = surfaceStage->texture.hasMatrix;
					break;
				}
				}
			}

			// draw the final interaction
			RB_SubmittInteraction( &inter, interactionList );
		}
	}
}

void RB_GLSL_SubmitDrawInteractions( const InteractionList& interactionList ) {
	if(interactionList.IsEmpty())
		return;

	// perform setup here that will be constant for all interactions
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	GL_UseProgram( interactionProgram );

	fhRenderProgram::SetShading( r_shading.GetInteger() );
	fhRenderProgram::SetSpecularExp( r_specularExp.GetFloat() );

	if (backEnd.vLight->lightDef->ShadowMode() == shadowMode_t::ShadowMap) {
		const idVec4 globalLightOrigin = idVec4( backEnd.vLight->globalLightOrigin, 1 );
		fhRenderProgram::SetGlobalLightOrigin( globalLightOrigin );

		const float shadowBrightness = backEnd.vLight->lightDef->ShadowBrightness();
		const float shadowSoftness = backEnd.vLight->lightDef->ShadowSoftness();
		fhRenderProgram::SetShadowParams( idVec4( shadowSoftness, shadowBrightness, 0, 0 ) );

		if (backEnd.vLight->lightDef->parms.pointLight) {
			//point light
			fhRenderProgram::SetShadowMappingMode( 1 );
			fhRenderProgram::SetPointLightProjectionMatrices( &backEnd.shadowViewProjection[0][0] );
		}
		else {
			//projected light
			fhRenderProgram::SetShadowMappingMode( 2 );
			fhRenderProgram::SetSpotLightProjectionMatrix( backEnd.testProjectionMatrix );
		}
	}
	else {
		//no shadows
		fhRenderProgram::SetShadowMappingMode( 0 );
	}

	fhRenderProgram::SetProjectionMatrix( backEnd.viewDef->projectionMatrix );
	fhRenderProgram::SetPomMaxHeight( -1 );
	
	const viewEntity_t* currentSpace = nullptr;
	stageVertexColor_t currentVertexColor = (stageVertexColor_t)-1;
	bool currentPomEnabled = false;
	idScreenRect currentScissor;
	bool depthHackActive = false;
	bool currentHasBumpMatrix = false;
	bool currentHasDiffuseMatrix = false;
	bool currentHasSpecularMatrix = false;
	idVec4 currentDiffuseColor = idVec4(1,1,1,1);
	idVec4 currentSpecularColor = idVec4(1,1,1,1);

	fhRenderProgram::SetDiffuseColor( currentDiffuseColor );
	fhRenderProgram::SetSpecularColor( currentSpecularColor );
	fhRenderProgram::SetBumpMatrix( idVec4::identityS, idVec4::identityT );
	fhRenderProgram::SetSpecularMatrix( idVec4::identityS, idVec4::identityT );
	fhRenderProgram::SetDiffuseMatrix( idVec4::identityS, idVec4::identityT );


	if (r_useScissor.GetBool()) {
		glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		currentScissor.x1 = 0;
		currentScissor.y1 = 0;
		currentScissor.x2 = glConfig.vidWidth;
		currentScissor.y2 = glConfig.vidHeight;
	}

	const int num = interactionList.Num();
	for(int i = 0; i < num; ++i) {
		const auto& din = interactionList[i];

		const auto offset = vertexCache.Bind( din.surf->geo->ambientCache );
		GL_SetupVertexAttributes( fhVertexLayout::Draw, offset );				

		if(currentSpace != din.surf->space) {
			fhRenderProgram::SetModelMatrix( din.surf->space->modelMatrix );
			fhRenderProgram::SetModelViewMatrix( din.surf->space->modelViewMatrix );

			if (din.surf->space->modelDepthHack) {
				RB_EnterModelDepthHack( din.surf->space->modelDepthHack );
				fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );
				depthHackActive = true;
			}
			else if (din.surf->space->weaponDepthHack) {
				RB_EnterWeaponDepthHack();
				fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );
				depthHackActive = true;
			}
			else if ( depthHackActive ) {
				RB_LeaveDepthHack();
				fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );
				depthHackActive = false;
			}

			// change the scissor if needed
			if (r_useScissor.GetBool() && !currentScissor.Equals( din.surf->scissorRect )) {
				currentScissor = din.surf->scissorRect;
				glScissor( backEnd.viewDef->viewport.x1 + currentScissor.x1,
					backEnd.viewDef->viewport.y1 + currentScissor.y1,
					currentScissor.x2 + 1 - currentScissor.x1,
					currentScissor.y2 + 1 - currentScissor.y1 );
			}

			currentSpace = din.surf->space;
		}

		fhRenderProgram::SetLocalLightOrigin( din.localLightOrigin );
		fhRenderProgram::SetLocalViewOrigin( din.localViewOrigin );
		fhRenderProgram::SetLightProjectionMatrix( din.lightProjection[0], din.lightProjection[1], din.lightProjection[2] );
		fhRenderProgram::SetLightFallOff( din.lightProjection[3] );

		if(din.hasBumpMatrix) {
			fhRenderProgram::SetBumpMatrix( din.bumpMatrix[0], din.bumpMatrix[1] );
			currentHasBumpMatrix = true;
		}
		else if(currentHasBumpMatrix) {
			fhRenderProgram::SetBumpMatrix( idVec4::identityS, idVec4::identityT );
			currentHasBumpMatrix = false;
		}
		
		if(din.hasDiffuseMatrix) {
			fhRenderProgram::SetDiffuseMatrix( din.diffuseMatrix[0], din.diffuseMatrix[1] );
			currentHasDiffuseMatrix = true;
		}
		else if(currentHasDiffuseMatrix) {
			fhRenderProgram::SetDiffuseMatrix( idVec4::identityS, idVec4::identityT );
			currentHasDiffuseMatrix = false;
		}
		
		if(din.hasSpecularMatrix) {
			fhRenderProgram::SetSpecularMatrix( din.specularMatrix[0], din.specularMatrix[1] );
			currentHasSpecularMatrix = false;
		}
		else if(currentHasSpecularMatrix) {
			fhRenderProgram::SetSpecularMatrix( idVec4::identityS, idVec4::identityT );
			currentHasSpecularMatrix = false;
		}

		if(currentVertexColor != din.vertexColor) {
			switch (din.vertexColor) {
			case SVC_IGNORE:
				fhRenderProgram::SetColorModulate( idVec4::zero );
				fhRenderProgram::SetColorAdd( idVec4::one );
				break;
			case SVC_MODULATE:
				fhRenderProgram::SetColorModulate( idVec4::one );
				fhRenderProgram::SetColorAdd( idVec4::zero );
				break;
			case SVC_INVERSE_MODULATE:
				fhRenderProgram::SetColorModulate( idVec4::negOne );
				fhRenderProgram::SetColorAdd( idVec4::one );
				break;
			}
			currentVertexColor = din.vertexColor;
		}

		if(din.diffuseColor != currentDiffuseColor) {
			fhRenderProgram::SetDiffuseColor( din.diffuseColor );
			currentDiffuseColor = din.diffuseColor;
		}

		if (din.specularColor != currentSpecularColor) {
			fhRenderProgram::SetSpecularColor( din.specularColor );
			currentSpecularColor = din.specularColor;
		}		

		const bool pomEnabled = r_pomEnabled.GetBool() && din.specularImage->hasAlpha;
		if (pomEnabled != currentPomEnabled) {
			if(pomEnabled) {
				fhRenderProgram::SetPomMaxHeight( r_pomMaxHeight.GetFloat() );
			} 
			else {
				fhRenderProgram::SetPomMaxHeight( -1 );
			}
		}
		
		din.bumpImage->Bind( 1 );		
		din.lightFalloffImage->Bind( 2 );
		din.lightImage->Bind( 3 );
		din.diffuseImage->Bind( 4 );
		din.specularImage->Bind( 5 );

		// draw it
		RB_DrawElementsWithCounters( din.surf->geo );
	}

	if(depthHackActive) {
		RB_LeaveDepthHack();
		fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );
	}

	if (r_useScissor.GetBool()) {
		glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		backEnd.currentScissor.x1 = 0;
		backEnd.currentScissor.y1 = 0;
		backEnd.currentScissor.x2 = glConfig.vidWidth;
		backEnd.currentScissor.y2 = glConfig.vidHeight;
	}
}

/*
==================
RB_GLSL_DrawInteractions
==================
*/
void RB_GLSL_DrawInteractions(void) {
  viewLight_t		*vLight;
  const idMaterial	*lightShader;

  InteractionList interactionList;

  //
  // for each light, perform adding and shadowing
  //
  for (vLight = backEnd.viewDef->viewLights; vLight; vLight = vLight->next) {
    backEnd.vLight = vLight;

    // do fogging later
    if (vLight->lightShader->IsFogLight()) {
      continue;
    }
    if (vLight->lightShader->IsBlendLight()) {
      continue;
    }

    if (!vLight->localInteractions && !vLight->globalInteractions
      && !vLight->translucentInteractions) {
      continue;
    }	

	if(vLight->lightDef->ShadowMode() == shadowMode_t::ShadowMap)
		RB_RenderShadowMaps(vLight);
	
    lightShader = vLight->lightShader;

    // clear the stencil buffer if needed
    if (vLight->globalShadows || vLight->localShadows) {
      backEnd.currentScissor = vLight->scissorRect;
      if (r_useScissor.GetBool()) {
        glScissor(backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
          backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
          backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
          backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1);
      }
      glClear(GL_STENCIL_BUFFER_BIT);
    }
    else {
      // no shadows, so no need to read or write the stencil buffer
      // we might in theory want to use GL_ALWAYS instead of disabling
      // completely, to satisfy the invarience rules
      glStencilFunc(GL_ALWAYS, 128, 255);
    }

	if(r_renderList.GetBool()) {
		RB_GLSL_StencilShadowPass( vLight->globalShadows );
		interactionList.Clear();
		RB_GLSL_CreateDrawInteractions( vLight->localInteractions, interactionList );
		RB_GLSL_SubmitDrawInteractions(interactionList);
				
		RB_GLSL_StencilShadowPass( vLight->localShadows );
		interactionList.Clear();
		RB_GLSL_CreateDrawInteractions( vLight->globalInteractions, interactionList );
		RB_GLSL_SubmitDrawInteractions( interactionList );		
	}
	else {
		RB_GLSL_StencilShadowPass( vLight->globalShadows );
		RB_GLSL_CreateDrawInteractions( vLight->localInteractions );
		RB_GLSL_StencilShadowPass( vLight->localShadows );
		RB_GLSL_CreateDrawInteractions( vLight->globalInteractions );
	}
	
    // translucent surfaces never get stencil shadowed
    if (r_skipTranslucent.GetBool()) {
      continue;
    }

    glStencilFunc(GL_ALWAYS, 128, 255);

    backEnd.depthFunc = GLS_DEPTHFUNC_LESS;

	if(r_renderList.GetBool()) {
		interactionList.Clear();
		RB_GLSL_CreateDrawInteractions( vLight->translucentInteractions, interactionList );
		RB_GLSL_SubmitDrawInteractions( interactionList );
	}
	else {
		RB_GLSL_CreateDrawInteractions(vLight->translucentInteractions);
	}

    backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
  }

  // disable stencil shadow test
  glStencilFunc(GL_ALWAYS, 128, 255);
}



static bool RB_GLSL_CreateShaderStage(const drawSurf_t* surf, const shaderStage_t* pStage, drawStage_t& drawStage) {
	// set the color  
	float color[4];
	color[0] = surf->shaderRegisters[pStage->color.registers[0]];
	color[1] = surf->shaderRegisters[pStage->color.registers[1]];
	color[2] = surf->shaderRegisters[pStage->color.registers[2]];
	color[3] = surf->shaderRegisters[pStage->color.registers[3]];

	// skip the entire stage if an add would be black
	if ((pStage->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE)
		&& color[0] <= 0 && color[1] <= 0 && color[2] <= 0) {
		return false;
	}

	// skip the entire stage if a blend would be completely transparent
	if ((pStage->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA)
		&& color[3] <= 0) {
		return false;
	}

	if (pStage->texture.texgen == TG_DIFFUSE_CUBE) {
		return false;
	}
	else if (pStage->texture.texgen == TG_SKYBOX_CUBE || pStage->texture.texgen == TG_WOBBLESKY_CUBE) {
		drawStage.program = skyboxProgram;
		
		if (pStage->texture.texgen == TG_WOBBLESKY_CUBE) {
			R_CreateWobbleskyTexMatrix( surf, backEnd.viewDef->floatTime, drawStage.textureMatrix );
		}

		idVec4 localViewOrigin;
		R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3() );
		localViewOrigin[3] = 1.0f;

		drawStage.localViewOrigin = localViewOrigin;
		drawStage.vertexLayout = fhVertexLayout::DrawPosColorOnly;
	}
	else if (pStage->texture.texgen == TG_SCREEN) {
		return false;
	}
	else if (pStage->texture.texgen == TG_GLASSWARP) {
		return false;
	}
	else if (pStage->texture.texgen == TG_REFLECT_CUBE) {
		drawStage.program = bumpyEnvProgram;

		idVec4 localViewOrigin;
		R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3() );
		localViewOrigin[3] = 1.0f;

		drawStage.localViewOrigin = localViewOrigin;
		
		//TODO(johl): do we really need this? bumpyenv.vp uses the texture matrix, but it's always the identiy matrix anyway?
		memcpy(drawStage.textureMatrix, &mat4_identity, sizeof(mat4_identity));

		// see if there is also a bump map specified
		if (const shaderStage_t *bumpStage = surf->material->GetBumpStage()) {
			RB_GetShaderTextureMatrix( surf->shaderRegisters, &bumpStage->texture, drawStage.bumpMatrix );
			drawStage.hasBumpMatrix = true;
			drawStage.textures[2] = bumpStage->texture.image;
		}
		else {
			drawStage.textures[2] = globalImages->flatNormalMap;
		}

		drawStage.vertexLayout = fhVertexLayout::Draw;
	}
	else {

		//prefer depth blend settings from material. If material does not define a 
		// depth blend mode, look at the geometry for depth blend settings (usually
		// set by particle systems for soft particles)

		drawStage.depthBlendMode = pStage->depthBlendMode;
		drawStage.depthBlendRange = pStage->depthBlendRange;

		if (drawStage.depthBlendMode == DBM_UNDEFINED) {
			drawStage.depthBlendMode = surf->geo->depthBlendMode;
			drawStage.depthBlendRange = surf->geo->depthBlendRange;
		}

		if (drawStage.depthBlendMode == DBM_AUTO) {
			if (pStage->drawStateBits & (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE))
				drawStage.depthBlendMode = DBM_COLORALPHA_ZERO;
			else if (pStage->drawStateBits & (GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO))
				drawStage.depthBlendMode = DBM_COLORALPHA_ONE;
			else
				drawStage.depthBlendMode = DBM_OFF;
		}

		if (drawStage.depthBlendMode != DBM_OFF && drawStage.depthBlendRange > 0.0f) {
			drawStage.program = depthblendProgram;
			drawStage.textures[2] = globalImages->currentDepthImage;
		}
		else {
			drawStage.program = defaultProgram;
		}

		drawStage.vertexLayout = fhVertexLayout::DrawPosColorTexOnly;
	}

	drawStage.cinematic = pStage->texture.cinematic;

	// set the state
	drawStage.drawStateBits = pStage->drawStateBits;
	drawStage.vertexColor = pStage->vertexColor;
	
	// set privatePolygonOffset if necessary
	if (pStage->privatePolygonOffset) {
		drawStage.polygonOffset = pStage->privatePolygonOffset;
	}

	if (pStage->texture.image) {
		if (pStage->texture.hasMatrix) {
			RB_GetShaderTextureMatrix( surf->shaderRegisters, &pStage->texture, drawStage.bumpMatrix );
			drawStage.hasBumpMatrix = true;
		}

		drawStage.textures[1] = pStage->texture.image;		
	}

	drawStage.diffuseColor = idVec4(color);
	return true;
}


static void RB_GLSL_CreateShaderPasses(const drawSurf_t* surf, StageRenderList& renderlist) {
	const srfTriangles_t* tri = surf->geo;
	const idMaterial* shader = surf->material;

	if (!shader->HasAmbient()) {
		return;
	}

	if (shader->IsPortalSky()) {
		return;
	}

	// some deforms may disable themselves by setting numIndexes = 0
	if (!tri->numIndexes) {
		return;
	}

	if (!tri->ambientCache) {
		common->Printf( "RB_T_RenderShaderPasses: !tri->ambientCache\n" );
		return;
	}

	// get the expressions for conditionals / color / texcoords
	const float	*regs = surf->shaderRegisters;

	// get polygon offset if necessary
	const float polygonOffset = shader->TestMaterialFlag( MF_POLYGONOFFSET ) ? shader->GetPolygonOffset() : 0;
	

	for (int stage = 0; stage < shader->GetNumStages(); stage++) {
		const shaderStage_t* pStage = shader->GetStage( stage );

		// check the enable condition
		if (regs[pStage->conditionRegister] == 0) {
			continue;
		}

		// skip the stages involved in lighting
		if (pStage->lighting != SL_AMBIENT) {
			continue;
		}

		// skip if the stage is ( GL_ZERO, GL_ONE ), which is used for some alpha masks
		if ((pStage->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE)) {
			continue;
		}

		drawStage_t drawstage;
		memset( &drawstage, 0, sizeof(drawstage) );
		drawstage.surf = surf;
		drawstage.cullType = shader->GetCullType();
		drawstage.polygonOffset = polygonOffset;
		drawstage.depthBlendMode = DBM_OFF;
		drawstage.bumpMatrix[0] = idVec4::identityS;
		drawstage.bumpMatrix[1] = idVec4::identityT;
		drawstage.hasBumpMatrix = false;
		memcpy(drawstage.textureMatrix, &mat4_identity, sizeof(mat4_identity));

		if (glslShaderStage_t *glslStage = pStage->glslStage) { // see if we are a glsl-style stage

			if (r_skipGlsl.GetBool())
				continue;

			if (!glslStage->program)
				continue;

			drawstage.program = glslStage->program;
			drawstage.drawStateBits = pStage->drawStateBits;
			drawstage.numShaderparms = Min(glslStage->numShaderParms, 4);
			for (int i = 0; i < drawstage.numShaderparms; i++) {
				idVec4& parm = drawstage.shaderparms[i];
				parm[0] = regs[glslStage->shaderParms[i][0]];
				parm[1] = regs[glslStage->shaderParms[i][1]];
				parm[2] = regs[glslStage->shaderParms[i][2]];
				parm[3] = regs[glslStage->shaderParms[i][3]];
			}

			// set textures
			for (int i = 0; i < glslStage->numShaderMaps && i < 4; i++) {
				drawstage.textures[i] = glslStage->shaderMap[i];
			}

			drawstage.vertexLayout = fhVertexLayout::Draw;

			renderlist.Append(drawstage);
		}
		else {
			if( RB_GLSL_CreateShaderStage(surf, pStage, drawstage) ) {
				renderlist.Append(drawstage);
			}
		}
	}
}

void RB_GLSL_SubmitStageRenderList(const StageRenderList& renderlist) {

	const fhRenderProgram* currentProgram = nullptr;
	const viewEntity_t* currentSpace = nullptr;
	stageVertexColor_t currentVertexColor = (stageVertexColor_t)-1;
	bool currentBumpMatrix = true;
	idScreenRect currentScissor;

	const int num = renderlist.Num();
	for(int i = 0; i < num; ++i) {
		const drawStage_t& drawstage = renderlist[i];

		if(currentProgram != drawstage.program) {
			GL_UseProgram(drawstage.program);
			fhRenderProgram::SetProjectionMatrix(backEnd.viewDef->projectionMatrix);
			fhRenderProgram::SetClipRange( backEnd.viewDef->viewFrustum.GetNearDistance(), backEnd.viewDef->viewFrustum.GetFarDistance() );

			// current render
			const int w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
			const int h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
			fhRenderProgram::SetCurrentRenderSize(
				idVec2( globalImages->currentRenderImage->uploadWidth, globalImages->currentRenderImage->uploadHeight ),
				idVec2( w, h ) );

			currentProgram = drawstage.program;
			currentSpace = nullptr;
			currentVertexColor = (stageVertexColor_t)-1;
			currentBumpMatrix = true;
		}

		if(currentSpace != drawstage.surf->space) {			
			fhRenderProgram::SetModelViewMatrix(drawstage.surf->space->modelViewMatrix);
			fhRenderProgram::SetModelMatrix(drawstage.surf->space->modelMatrix);

			if (r_useScissor.GetBool() && !backEnd.currentScissor.Equals( drawstage.surf->scissorRect )) {
				currentScissor = drawstage.surf->scissorRect;
				glScissor( backEnd.viewDef->viewport.x1 + currentScissor.x1,
					backEnd.viewDef->viewport.y1 + currentScissor.y1,
					currentScissor.x2 + 1 - currentScissor.x1,
					currentScissor.y2 + 1 - currentScissor.y1 );
			}

			currentSpace = drawstage.surf->space;
		}

		const auto offset = vertexCache.Bind(drawstage.surf->geo->ambientCache); 
		GL_SetupVertexAttributes(drawstage.vertexLayout, offset);

		GL_Cull(drawstage.cullType);

		if(drawstage.polygonOffset) {
			glEnable( GL_POLYGON_OFFSET_FILL );
			glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * drawstage.polygonOffset );
		}

		if (drawstage.surf->space->weaponDepthHack) {
			RB_EnterWeaponDepthHack();
			fhRenderProgram::SetProjectionMatrix(GL_ProjectionMatrix.Top());
		}

		if (drawstage.surf->space->modelDepthHack != 0.0f) {
			RB_EnterModelDepthHack( drawstage.surf->space->modelDepthHack );
			fhRenderProgram::SetProjectionMatrix(GL_ProjectionMatrix.Top());
		}

		if(drawstage.hasBumpMatrix) {
			fhRenderProgram::SetBumpMatrix(drawstage.bumpMatrix[0], drawstage.bumpMatrix[1]);  
			currentBumpMatrix = true;
		} else if(currentBumpMatrix) {
			fhRenderProgram::SetBumpMatrix(idVec4::identityS, idVec4::identityT);  
			currentBumpMatrix = false;
		}

		fhRenderProgram::SetLocalViewOrigin(drawstage.localViewOrigin);
		fhRenderProgram::SetDiffuseColor(drawstage.diffuseColor);		
		fhRenderProgram::SetTextureMatrix(drawstage.textureMatrix);
		fhRenderProgram::SetDepthBlendRange( drawstage.depthBlendRange );
		fhRenderProgram::SetDepthBlendMode( static_cast<int>(drawstage.depthBlendMode) );		

		if(currentVertexColor != drawstage.vertexColor) {
			switch (drawstage.vertexColor) {
			case SVC_IGNORE:
				fhRenderProgram::SetColorModulate( idVec4::zero );
				fhRenderProgram::SetColorAdd( idVec4::one );
				break;
			case SVC_MODULATE:
				fhRenderProgram::SetColorModulate( idVec4::one );
				fhRenderProgram::SetColorAdd( idVec4::zero );
				break;
			case SVC_INVERSE_MODULATE:
				fhRenderProgram::SetColorModulate( idVec4::negOne );
				fhRenderProgram::SetColorAdd( idVec4::one );
				break;
			}
			currentVertexColor = drawstage.vertexColor;
		}

		for(int i = 0; i < 4; ++i) {			
			if(drawstage.textures[i])
				drawstage.textures[i]->Bind(i);
		}	

		if(drawstage.cinematic) {
			if (r_skipDynamicTextures.GetBool()) {
				globalImages->defaultImage->Bind( 1 );
			}
			else {
				// offset time by shaderParm[7] (FIXME: make the time offset a parameter of the shader?)
				// We make no attempt to optimize for multiple identical cinematics being in view, or
				// for cinematics going at a lower framerate than the renderer.
				cinData_t cin = drawstage.cinematic->ImageForTime( (int)(1000 * (backEnd.viewDef->floatTime + backEnd.viewDef->renderView.shaderParms[11])) );

				if (cin.image) {
					globalImages->cinematicImage->UploadScratch( 1, cin.image, cin.imageWidth, cin.imageHeight );
				}
				else {
					globalImages->blackImage->Bind( 1 );
				}
			}
		}

		GL_State(drawstage.drawStateBits);

		RB_DrawElementsWithCounters(drawstage.surf->geo);

		if (drawstage.polygonOffset) {
			glDisable( GL_POLYGON_OFFSET_FILL );
		}

		if (drawstage.surf->space->weaponDepthHack || drawstage.surf->space->modelDepthHack != 0.0f) {
			RB_LeaveDepthHack();
			fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );
		}
	}

	if (r_useScissor.GetBool()) {
		glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		backEnd.currentScissor.x1 = 0;
		backEnd.currentScissor.y1 = 0;
		backEnd.currentScissor.x2 = glConfig.vidWidth;
		backEnd.currentScissor.y2 = glConfig.vidHeight;
	}

	GL_UseProgram( nullptr );
	GL_SelectTexture( 0 );
}

int RB_GLSL_CreateStageRenderList(drawSurf_t **drawSurfs, int numDrawSurfs, StageRenderList& renderlist) {
	int				i;

	// only obey skipAmbient if we are rendering a view
	if (backEnd.viewDef->viewEntitys && r_skipAmbient.GetBool()) {
		return numDrawSurfs;
	}

	RB_LogComment( "---------- RB_GLSL_CreateStageRenderList ----------\n" );

	// if we are about to draw the first surface that needs
	// the rendering in a texture, copy it over
	if (drawSurfs[0]->material->GetSort() >= SS_POST_PROCESS) {
		if (r_skipPostProcess.GetBool()) {
			return 0;
		}

		// only dump if in a 3d view
		if (backEnd.viewDef->viewEntitys) {
			globalImages->currentRenderImage->CopyFramebuffer( backEnd.viewDef->viewport.x1,
				backEnd.viewDef->viewport.y1, backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
				backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1, true );
		}
		backEnd.currentRenderCopied = true;
	}

	for (i = 0; i < numDrawSurfs; i++) {
		if (drawSurfs[i]->material->SuppressInSubview()) {
			continue;
		}

		if (backEnd.viewDef->isXraySubview && drawSurfs[i]->space->entityDef) {
			if (drawSurfs[i]->space->entityDef->parms.xrayIndex != 2) {
				continue;
			}
		}

		// we need to draw the post process shaders after we have drawn the fog lights
		if (drawSurfs[i]->material->GetSort() >= SS_POST_PROCESS
			&& !backEnd.currentRenderCopied) {
			break;
		}

		RB_GLSL_CreateShaderPasses( drawSurfs[i], renderlist );
	}

	return i;
}