#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include FT_OUTLINE_H

#include "blocks/blocktypes.h"
#include "parser.h"
#include "swfoutput.h"

static FT_Library library;
static FT_Face face;
static double ratio_EM;

struct Movie m;
struct SWF_SETBACKGROUNDCOLOR bg = {{0}};
struct SWF_DEFINEFONT2 swffont = {0};
struct SWF_SHOWFRAME showframe = {0};

static int BITS_LENGTH[] = {
//  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, A, B, C, D, E, F
    0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4
};

int getMinBitsU( unsigned int v ) {
    int n = 0;
    if( (v & ~0xffff) != 0 ) {
        n+=16; v >>= 16;
    }
    if( (v & ~0x00ff) != 0 ) {
        n+= 8; v >>=  8;
    }
    if( (v & ~0x000f) != 0 ) {
        n+= 4; v >>=  4;
    }
    n += BITS_LENGTH[v];
    return n;
}

int getMinBitsS( int v ) {
    if( v < 0 ) v = -v;
    return getMinBitsU( v )+1;
}


int
max(int a, int b)
{
if( a < 0 ) a=-a;
if( b < 0 ) b=-b;
return a>b?a:b;
}

/*
 * Functions to decompose the outlines
 */

static SWF_SHAPE *curshape;
static int last_x,last_y;

static int
outl_moveto(
	const FT_Vector *to,
	void *unused
)
{
SWF_STYLECHANGERECORD *stylerec;
int	dx = (int)(to->x*ratio_EM);
int	dy = -(int)(to->y*ratio_EM);

	//fprintf(stderr,"moveto(%d,%d)\n",dx,dy);
	curshape->ShapeRecords = (SWF_SHAPERECORD *)realloc(curshape->ShapeRecords, ((curshape->NumShapeRecords+1)*sizeof(SWF_SHAPERECORD)));
	stylerec=&(curshape->ShapeRecords[curshape->NumShapeRecords++].StyleChange);
	memset(stylerec,0,sizeof(SWF_SHAPERECORD));
	stylerec->TypeFlag=0;
	if( curshape->NumShapeRecords == 1 ) {
		stylerec->StateLineStyle=1;
		stylerec->LineStyle=0;
		stylerec->StateFillStyle1=1;
		stylerec->FillStyle1=1;
	}
	stylerec->StateMoveTo=1;
	stylerec->MoveBits=getMinBitsS( max(dx, dy) );
	stylerec->MoveDeltaX=dx;
	stylerec->MoveDeltaY=dy;
	last_x=dx;
	last_y=dy;

	return 0;
}

static int
outl_lineto(
	const FT_Vector *to,
	void *unused
)
{
SWF_STRAIGHTEDGERECORD *linerec;
int	x = (int)(to->x*ratio_EM);
int	y = -(int)(to->y*ratio_EM);
int	dx = x-last_x;
int	dy = y-last_y;

	last_x=x;
	last_y=y;

	//fprintf(stderr,"lineto(%d,%d)\n",dx,dy);
	curshape->ShapeRecords = (SWF_SHAPERECORD *)realloc(curshape->ShapeRecords, ((curshape->NumShapeRecords+1)*sizeof(SWF_SHAPERECORD)));
	linerec=&(curshape->ShapeRecords[curshape->NumShapeRecords++].StraightEdge);
	memset(linerec,0,sizeof(SWF_SHAPERECORD));
	linerec->TypeFlag=1;
	linerec->StraightEdge=1;
	linerec->NumBits=getMinBitsS( max(dx, dy) );
	if( linerec->NumBits < 3 ) linerec->NumBits=3;
	linerec->NumBits-=2;
	if( dx==0 ) {
		linerec->VertLineFlag=1;
		linerec->VLDeltaY=dy;
	} else if ( dy==0 ) {
		linerec->VLDeltaX=dx;
	} else {
		linerec->GeneralLineFlag=1;
		linerec->DeltaX=dx;
		linerec->DeltaY=dy;
	}

	return 0;
}

static int
outl_conicto(
	const FT_Vector *ctl,
	const FT_Vector *to,
	void *unused
)
{
SWF_CURVEDEDGERECORD *curverec;
int	cx = (int)(ctl->x*ratio_EM);
int	cy = -(int)(ctl->y*ratio_EM);
int	ax = (int)(to->x*ratio_EM);
int	ay = -(int)(to->y*ratio_EM);
int	dax = ax-cx;
int	day = ay-cy;
int	dcx = cx-last_x;
int	dcy = cy-last_y;

	//printf("conicto(%d,%d)\n",to->x,to->y);
	curshape->ShapeRecords = (SWF_SHAPERECORD *)realloc(curshape->ShapeRecords, ((curshape->NumShapeRecords+1)*sizeof(SWF_SHAPERECORD)));
	curverec=&(curshape->ShapeRecords[curshape->NumShapeRecords++].CurvedEdge);
	memset(curverec,0,sizeof(SWF_SHAPERECORD));
	curverec->TypeFlag=1;
	curverec->StraightFlag=0;
	curverec->NumBits=getMinBitsS( max(max(dax,day),max(dcx,dcy)) );
	if( curverec->NumBits < 3 ) curverec->NumBits=3;
	curverec->NumBits-=2;
	curverec->AnchorDeltaX=dax;
	curverec->AnchorDeltaY=day;
	curverec->ControlDeltaX=dcx;
	curverec->ControlDeltaY=dcy;

	last_x=ax;
	last_y=ay;

	return 0;
}

static int
outl_cubicto(
	const FT_Vector *control1,
	const FT_Vector *control2,
	const FT_Vector *to,
	void *unused
)
{
	fprintf(stderr,"cubicto(%d,%d)\n",(int)to->x,(int)to->y);
	return 0;
}

static FT_Outline_Funcs ft_outl_funcs = {
	outl_moveto,
	outl_lineto,
	outl_conicto,
	outl_cubicto,
	0,
	0
};

int
main(int argc, char *argv)
{
SWF_SHAPE *shape;
SWF_ENDSHAPERECORD *endrec;
SWF_RECT *bbox;
FT_Error error;
FT_ULong	charcode;
FT_UInt	gindex;
FT_Outline *outline;
FT_Vector kern;
char *fname;
int i, j, k, maxcode=0;

/* Do some getopt stuff here */
fname = argv[1];

if( FT_Init_FreeType( &library ) ) {
	fprintf(stderr, "** FreeType initialization failed\n");
	exit(1);
}

if( (error = FT_New_Face( library, fname, 0, &face )) ) {
	if ( error == FT_Err_Unknown_File_Format )
		fprintf(stderr, "**** %s has format unknown to FreeType\n", fname);
	else
		fprintf(stderr, "**** Cannot access %s ****\n", fname);
	exit(1);
}

m.version = 4;
m.size = 8000;
m.frame.xMin = 0;
m.frame.xMax = 11000;
m.frame.yMin = 0;
m.frame.yMax = 9000;
m.rate = 0x0c00;
m.nFrames = 0;
outputHeader(&m);

/* Pull the data from the font, and build up the SWF_DEFINEFONT2 record */

ratio_EM = 1024.0/face->units_per_EM;

swffont.FontID = 1;
swffont.FontName = strdup(face->family_name);
swffont.FontNameLen=strlen(swffont.FontName);


swffont.FontFlagsHasLayout=1;
swffont.FontFlagsSmallText=1;

/*
for(i=0; i < face->num_charmaps; i++) {
                        fprintf(stdout, "found encoding pid=%d eid=%d\n",
                                face->charmaps[i]->platform_id,
                                face->charmaps[i]->encoding_id);
                }
*/
FT_Select_Charmap(face,FT_ENCODING_UNICODE);

if( strstr(face->style_name, "Bold") )
	swffont.FontFlagsFlagsBold = 1;
if( strstr(face->style_name, "Oblique") )
	swffont.FontFlagsFlagsItalics = 1;


/* Allocate the buffer space that we will be using */

/* allocate space for the larger format to be safe */
swffont.OffsetTable.UI32=(UI32 *)calloc(face->num_glyphs,sizeof(UI32));

swffont.GlyphShapeTable=(SWF_SHAPE *)calloc(face->num_glyphs,sizeof(SWF_SHAPE));

swffont.CodeTable=(int *)calloc(face->num_glyphs,sizeof(int));

swffont.FontAdvanceTable=(SI16 *)calloc(face->num_glyphs,sizeof(SI16));
swffont.FontBoundsTable=(SWF_RECT *)calloc(face->num_glyphs,sizeof(SWF_RECT));

/* Now, loop through the glyphs and pull out the data for each one */

i=0;
charcode = FT_Get_First_Char(face, &gindex );
while ( gindex != 0 ) {
	swffont.CodeTable[i]=charcode;
	maxcode=max(maxcode,charcode);
	if( FT_Load_Glyph(face, gindex, FT_LOAD_NO_BITMAP|FT_LOAD_NO_SCALE) ) {
		fprintf(stderr, "Can't load glyph %d, skipped\n", gindex);
                continue;
                }

	/* Get the shape info */
	shape=&(swffont.GlyphShapeTable[i]);
	shape->NumShapeRecords = 0;
	shape->ShapeRecords = (SWF_SHAPERECORD *)calloc(1,sizeof(SWF_SHAPERECORD));
	shape->NumFillBits = 1;

	outline=&(face->glyph->outline);
	curshape= shape;
	if( FT_Outline_Decompose(outline, &ft_outl_funcs, NULL) ) {
		fprintf(stderr,"Can't decompose outline for glyph %d\n", gindex);
		continue;
		}
	/*Put an ENDSHAPE record at the end */
	curshape->ShapeRecords = (SWF_SHAPERECORD *)realloc(curshape->ShapeRecords, ((curshape->NumShapeRecords+1)*sizeof(SWF_SHAPERECORD)));
	endrec=&(curshape->ShapeRecords[curshape->NumShapeRecords++].EndShape);
	memset(endrec,0,sizeof(SWF_SHAPERECORD));
	endrec->TypeFlag=0;
	endrec->EndOfShape=0;

	/* Fill in the FontAdvanceTable */
	swffont.FontAdvanceTable[i] = (int)(face->glyph->advance.x*ratio_EM);

	/* Fill in the FontBoundsTable even though it isn't used */
	bbox=&(swffont.FontBoundsTable[i]);
	bbox->Nbits=12;
	bbox->Xmin=-1024;
	bbox->Xmax=1024;
	bbox->Ymin=-1024;
	bbox->Ymax=1024;

	/* Done with this char */
	charcode = FT_Get_Next_Char(face, charcode, &gindex);
	i++;
}
swffont.NumGlyphs = i;

swffont.FontAscent = (int)(face->ascender*ratio_EM);
swffont.FontDecent = -(int)(face->descender*ratio_EM);
swffont.FontLeading = (int)((face->height-face->ascender+face->descender)*ratio_EM);
fprintf(stderr,"FontLeading %d\n", swffont.FontLeading );

if( maxcode > 256 )
	swffont.FontFlagsWideCodes=1;

/* Now, loop through the kerning information and build up the kerning table */

k=0;
swffont.FontKerningTable=0;
for(i=0;i<swffont.NumGlyphs;i++) {
	for(j=0;j<swffont.NumGlyphs;j++) {
		if( FT_Get_Kerning(face, i, j, ft_kerning_unscaled, &kern) )
			continue;
		if( kern.x == 0 )
			continue;
	swffont.FontKerningTable = (struct SWF_KERNINGRECORD *)realloc(swffont.FontKerningTable, (k+1)*sizeof(struct SWF_KERNINGRECORD));
	swffont.FontKerningTable[k].FontKerningCode1 = swffont.CodeTable[i];
	swffont.FontKerningTable[k].FontKerningCode2 = swffont.CodeTable[j];
	swffont.FontKerningTable[k].FontKerningAdjustment = (int)(kern.x*ratio_EM);
	k++;
	}
}
swffont.KerningCount = k;

outputBlock(SWF_DEFINEFONT2,(SWF_Parserstruct *)&swffont,stdout,26,100);

if( FT_Done_Face(face) ) {
	fprintf(stderr, "Errors when closing the font file, ignored\n");
}
if( FT_Done_FreeType(library) ) {
	fprintf(stderr, "Errors when stopping FreeType, ignored\n");
}

outputTrailer(&m);

return 0;
}