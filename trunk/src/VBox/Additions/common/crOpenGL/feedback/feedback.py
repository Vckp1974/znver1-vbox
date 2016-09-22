# Copyright (c) 2001, Stanford University
# All rights reserved.
#
# See the file LICENSE.txt for information on redistributing this software.

from __future__ import print_function
import sys

import apiutil


apiutil.CopyrightC()

print("""
/* DO NOT EDIT - generated by feedback.py */
#include <stdio.h>
#include "cr_spu.h"
#include "feedbackspu.h"
#include "feedbackspu_proto.h"
#include "cr_packfunctions.h"
#include "cr_glstate.h"

""")

keys = apiutil.GetDispatchedFunctions(sys.argv[1]+"/APIspec.txt")

for func_name in keys:
	return_type = apiutil.ReturnType(func_name)
	params = apiutil.Parameters(func_name)
	if apiutil.FindSpecial( "feedback", func_name ):
		print('static %s FEEDBACKSPU_APIENTRY feedbackspu_%s( %s )' % ( return_type, func_name, apiutil.MakeDeclarationString(params) ))
		print('{')
		print('\tfeedback_spu.super.%s( %s );' % ( func_name, apiutil.MakeCallString(params) ))
		print('}')



print("""
#define CHANGE( name, func ) crSPUChangeInterface( (void *)&(feedback_spu.self), (void *)feedback_spu.self.name, (void *)((SPUGenericFunction) func) )
#define CHANGESWAP( name, swapfunc, regfunc ) crSPUChangeInterface( (void *)&(feedback_spu.self), (void *)feedback_spu.self.name, (void *)((SPUGenericFunction) (feedback_spu.swap ? swapfunc: regfunc )) )

static void __loadFeedbackAPI( void )
{
""")
for func_name in keys:
	return_type = apiutil.ReturnType(func_name)
	params = apiutil.Parameters(func_name)
	if apiutil.FindSpecial( "feedback", func_name ):
		print('\tCHANGE( %s, crStateFeedback%s );' % (func_name, func_name ))
print("""
}

static void __loadSelectAPI( void )
{
""")
for func_name in keys:
	if apiutil.FindSpecial( "select", func_name ):
		print('\tCHANGE( %s, crStateSelect%s );' % (func_name, func_name ))
	elif apiutil.FindSpecial( "feedback", func_name ):
		print('\tCHANGE( %s, feedbackspu_%s );' % (func_name, func_name ))
print("""
}

static void __loadRenderAPI( void )
{
""")

for func_name in keys:
	return_type = apiutil.ReturnType(func_name)
	if apiutil.FindSpecial( "feedback", func_name ) or apiutil.FindSpecial( "select", func_name ):
		print('\tCHANGE( %s, feedbackspu_%s );' % (func_name, func_name ))
print("""
}
""")

print("""
static GLint FEEDBACKSPU_APIENTRY feedbackspu_RenderMode ( GLenum mode )
{
	feedback_spu.render_mode = mode;

	switch (mode) {
		case GL_FEEDBACK:
			/*printf("Switching to Feedback API\\n");*/
			__loadFeedbackAPI( );
			break;
		case GL_SELECT:
			/*printf("Switching to Selection API\\n");*/
			__loadSelectAPI( );
			break;
		case GL_RENDER:
			/*printf("Switching to Render API\\n");*/
			__loadRenderAPI( );
			break;
	}

	return crStateRenderMode( mode );
}

static void FEEDBACKSPU_APIENTRY feedbackspu_Begin ( GLenum mode )
{
	if (feedback_spu.render_mode == GL_FEEDBACK)
		crStateFeedbackBegin( mode );
	else if (feedback_spu.render_mode == GL_SELECT)
		crStateSelectBegin( mode );
	else
	{
		crStateBegin( mode );
		feedback_spu.super.Begin( mode );
	}
}

static void FEEDBACKSPU_APIENTRY feedbackspu_End ( void )
{
	if (feedback_spu.render_mode == GL_FEEDBACK)
		crStateFeedbackEnd( );
	else if (feedback_spu.render_mode == GL_SELECT)
		crStateSelectEnd( );
	else
	{
		crStateEnd( );
		feedback_spu.super.End( );
	}
}

static void FEEDBACKSPU_APIENTRY feedbackspu_Bitmap ( GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap )
{
	crStateBitmap( width, height, xorig, yorig, xmove, ymove, bitmap );

	if (feedback_spu.render_mode == GL_FEEDBACK)
		crStateFeedbackBitmap( width, height, xorig, yorig, xmove, ymove, bitmap );
	else if (feedback_spu.render_mode == GL_SELECT)
		crStateSelectBitmap( width, height, xorig, yorig, xmove, ymove, bitmap );
	else
		feedback_spu.super.Bitmap( width, height, xorig, yorig, xmove, ymove, bitmap );
}

static void FEEDBACKSPU_APIENTRY feedbackspu_CopyPixels( GLint x, GLint y, GLsizei width, GLsizei height, GLenum type )
{
	if (feedback_spu.render_mode == GL_FEEDBACK)
		crStateFeedbackCopyPixels( x, y, width, height, type );
	else if (feedback_spu.render_mode == GL_SELECT)
		crStateSelectCopyPixels( x, y, width, height, type );
	else
		feedback_spu.super.CopyPixels( x, y, width, height, type );
}

static void FEEDBACKSPU_APIENTRY feedbackspu_DrawPixels( GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels )
{
	if (feedback_spu.render_mode == GL_FEEDBACK)
		crStateFeedbackDrawPixels( width, height, format, type, pixels );
	else if (feedback_spu.render_mode == GL_SELECT)
		crStateSelectDrawPixels( width, height, format, type, pixels );
	else
		feedback_spu.super.DrawPixels( width, height, format, type, pixels );
}

static void FEEDBACKSPU_APIENTRY feedbackspu_GetBooleanv( GLenum pname, GLboolean *params )

{
	if (pname == GL_FEEDBACK_BUFFER_SIZE ||
	    pname == GL_FEEDBACK_BUFFER_TYPE ||
	    pname == GL_SELECTION_BUFFER_SIZE)
		crStateFeedbackGetBooleanv( pname, params );
	else
	if (pname == GL_VIEWPORT && feedback_spu.default_viewport)
		crStateGetBooleanv( pname, params );
	else
		feedback_spu.super.GetBooleanv( pname, params );
}

static void FEEDBACKSPU_APIENTRY feedbackspu_GetDoublev( GLenum pname, GLdouble *params )

{
	if (pname == GL_FEEDBACK_BUFFER_SIZE ||
	    pname == GL_FEEDBACK_BUFFER_TYPE ||
	    pname == GL_SELECTION_BUFFER_SIZE)
		crStateFeedbackGetDoublev( pname, params );
	else
	if (pname == GL_VIEWPORT && feedback_spu.default_viewport)
		crStateGetDoublev( pname, params );
	else
		feedback_spu.super.GetDoublev( pname, params );
}

static void FEEDBACKSPU_APIENTRY feedbackspu_GetFloatv( GLenum pname, GLfloat *params )

{
	if (pname == GL_FEEDBACK_BUFFER_SIZE ||
	    pname == GL_FEEDBACK_BUFFER_TYPE ||
	    pname == GL_SELECTION_BUFFER_SIZE)
		crStateFeedbackGetFloatv( pname, params );
	else
	if (pname == GL_VIEWPORT && feedback_spu.default_viewport)
		crStateGetFloatv( pname, params );
	else
		feedback_spu.super.GetFloatv( pname, params );
}

static void FEEDBACKSPU_APIENTRY feedbackspu_GetIntegerv( GLenum pname, GLint *params )

{
	if (pname == GL_FEEDBACK_BUFFER_SIZE ||
	    pname == GL_FEEDBACK_BUFFER_TYPE ||
	    pname == GL_SELECTION_BUFFER_SIZE)
		crStateFeedbackGetIntegerv( pname, params );
	else
	if (pname == GL_VIEWPORT && feedback_spu.default_viewport)
		crStateGetIntegerv( pname, params );
	else
		feedback_spu.super.GetIntegerv( pname, params );
}

SPUNamedFunctionTable _cr_feedback_table[] = {
""")

for func_name in keys:
	if apiutil.FindSpecial( "feedback_state", func_name ):
		print('\t{ "%s", (SPUGenericFunction) feedbackspu_%s }, ' % ( func_name, func_name ))
print("""
	{ "GetBooleanv", (SPUGenericFunction) feedbackspu_GetBooleanv },
	{ "GetDoublev", (SPUGenericFunction) feedbackspu_GetDoublev },
	{ "GetFloatv", (SPUGenericFunction) feedbackspu_GetFloatv },
	{ "GetIntegerv", (SPUGenericFunction) feedbackspu_GetIntegerv },
	{ "FeedbackBuffer", (SPUGenericFunction) crStateFeedbackBuffer },
	{ "SelectBuffer", (SPUGenericFunction) crStateSelectBuffer },
	{ "InitNames", (SPUGenericFunction) crStateInitNames },
	{ "LoadName", (SPUGenericFunction) crStateLoadName },
	{ "PushName", (SPUGenericFunction) crStatePushName },
	{ "PopName", (SPUGenericFunction) crStatePopName },
	{ "Begin", (SPUGenericFunction) feedbackspu_Begin },
	{ "End", (SPUGenericFunction) feedbackspu_End },
	{ "Bitmap", (SPUGenericFunction) feedbackspu_Bitmap },
	{ "CopyPixels", (SPUGenericFunction) feedbackspu_CopyPixels },
	{ "DrawPixels", (SPUGenericFunction) feedbackspu_DrawPixels },
	{ "TexCoord1d", (SPUGenericFunction) feedbackspu_TexCoord1d },
	{ "TexCoord1dv", (SPUGenericFunction) feedbackspu_TexCoord1dv },
	{ "TexCoord1f", (SPUGenericFunction) feedbackspu_TexCoord1f },
	{ "TexCoord1fv", (SPUGenericFunction) feedbackspu_TexCoord1fv },
	{ "TexCoord1s", (SPUGenericFunction) feedbackspu_TexCoord1s },
	{ "TexCoord1sv", (SPUGenericFunction) feedbackspu_TexCoord1sv },
	{ "TexCoord1i", (SPUGenericFunction) feedbackspu_TexCoord1i },
	{ "TexCoord1iv", (SPUGenericFunction) feedbackspu_TexCoord1iv },
	{ "TexCoord2d", (SPUGenericFunction) feedbackspu_TexCoord2d },
	{ "TexCoord2dv", (SPUGenericFunction) feedbackspu_TexCoord2dv },
	{ "TexCoord2f", (SPUGenericFunction) feedbackspu_TexCoord2f },
	{ "TexCoord2fv", (SPUGenericFunction) feedbackspu_TexCoord2fv },
	{ "TexCoord2s", (SPUGenericFunction) feedbackspu_TexCoord2s },
	{ "TexCoord2sv", (SPUGenericFunction) feedbackspu_TexCoord2sv },
	{ "TexCoord2i", (SPUGenericFunction) feedbackspu_TexCoord2i },
	{ "TexCoord2iv", (SPUGenericFunction) feedbackspu_TexCoord2iv },
	{ "TexCoord3d", (SPUGenericFunction) feedbackspu_TexCoord3d },
	{ "TexCoord3dv", (SPUGenericFunction) feedbackspu_TexCoord3dv },
	{ "TexCoord3f", (SPUGenericFunction) feedbackspu_TexCoord3f },
	{ "TexCoord3fv", (SPUGenericFunction) feedbackspu_TexCoord3fv },
	{ "TexCoord3s", (SPUGenericFunction) feedbackspu_TexCoord3s },
	{ "TexCoord3sv", (SPUGenericFunction) feedbackspu_TexCoord3sv },
	{ "TexCoord3i", (SPUGenericFunction) feedbackspu_TexCoord3i },
	{ "TexCoord3iv", (SPUGenericFunction) feedbackspu_TexCoord3iv },
	{ "TexCoord4d", (SPUGenericFunction) feedbackspu_TexCoord4d },
	{ "TexCoord4dv", (SPUGenericFunction) feedbackspu_TexCoord4dv },
	{ "TexCoord4f", (SPUGenericFunction) feedbackspu_TexCoord4f },
	{ "TexCoord4fv", (SPUGenericFunction) feedbackspu_TexCoord4fv },
	{ "TexCoord4s", (SPUGenericFunction) feedbackspu_TexCoord4s },
	{ "TexCoord4sv", (SPUGenericFunction) feedbackspu_TexCoord4sv },
	{ "TexCoord4i", (SPUGenericFunction) feedbackspu_TexCoord4i },
	{ "TexCoord4iv", (SPUGenericFunction) feedbackspu_TexCoord4iv },
	{ "RenderMode", (SPUGenericFunction) feedbackspu_RenderMode },
	{ NULL, NULL }
};
""")
