/* -------------------------------------------------------------------------------

Copyright (C) 1999-2007 id Software, Inc. and contributors.
For a list of contributors, see the accompanying CONTRIBUTORS file.

This file is part of GtkRadiant.

GtkRadiant is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

GtkRadiant is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GtkRadiant; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

-------------------------------------------------------------------------------

This code has been altered significantly from its original form, to support
several games based on the Quake III Arena engine, in the form of "Q3Map2."

------------------------------------------------------------------------------- */



/* marker */
#define MAIN_C



/* dependencies */
#include "q3map2.h"



/*
Random()
returns a pseudorandom number between 0 and 1
*/

vec_t Random( void )
{
	return (vec_t) rand() / RAND_MAX;
}



/*
ExitQ3Map()
cleanup routine
*/

static void ExitQ3Map( void )
{
	BSPFilesCleanup();
	if( mapDrawSurfs != NULL )
		free( mapDrawSurfs );
}



/* minimap stuff */

/* borrowed from light.c */
void WriteTGA24( char *filename, byte *data, int width, int height, qboolean flip );
typedef struct minimap_s
{
	bspModel_t *model;
	int width;
	int height;
	int samples;
	float *sample_offsets;
	float sharpen_boxmult;
	float sharpen_centermult;
	float *data1f;
	float *sharpendata1f;
	vec3_t mins, size;
}
minimap_t;
static minimap_t minimap;

qboolean BrushIntersectionWithLine(bspBrush_t *brush, vec3_t start, vec3_t dir, float *t_in, float *t_out)
{
	int i;
	qboolean in = qfalse, out = qfalse;
	bspBrushSide_t *sides = &bspBrushSides[brush->firstSide];

	for(i = 0; i < brush->numSides; ++i)
	{
		bspPlane_t *p = &bspPlanes[sides[i].planeNum];
		float sn = DotProduct(start, p->normal);
		float dn = DotProduct(dir, p->normal);
		if(dn == 0)
		{
			if(sn > p->dist)
				return qfalse; // outside!
		}
		else
		{
			float t = (p->dist - sn) / dn;
			if(dn < 0)
			{
				if(!in || t > *t_in)
				{
					*t_in = t;
					in = qtrue;
					// as t_in can only increase, and t_out can only decrease, early out
					if(out && *t_in >= *t_out)
						return qfalse;
				}
			}
			else
			{
				if(!out || t < *t_out)
				{
					*t_out = t;
					out = qtrue;
					// as t_in can only increase, and t_out can only decrease, early out
					if(in && *t_in >= *t_out)
						return qfalse;
				}
			}
		}
	}
	return in && out;
}

static float MiniMapSample(float x, float y)
{
	vec3_t org, dir;
	int i, bi;
	float t0, t1;
	float samp;
	bspBrush_t *b;
	int cnt;

	org[0] = x;
	org[1] = y;
	org[2] = 0;
	dir[0] = 0;
	dir[1] = 0;
	dir[2] = 1;

	cnt = 0;
	samp = 0;
	for(i = 0; i < minimap.model->numBSPBrushes; ++i)
	{
		bi = minimap.model->firstBSPBrush + i;
		if(opaqueBrushes[bi >> 3] & (1 << (bi & 7)))
		{
			b = &bspBrushes[bi];

			// sort out mins/maxs of the brush
			bspBrushSide_t *s = &bspBrushSides[b->firstSide];
			if(x < -bspPlanes[s[0].planeNum].dist)
				continue;
			if(x > +bspPlanes[s[1].planeNum].dist)
				continue;
			if(y < -bspPlanes[s[2].planeNum].dist)
				continue;
			if(y > +bspPlanes[s[3].planeNum].dist)
				continue;

			if(BrushIntersectionWithLine(b, org, dir, &t0, &t1))
			{
				samp += t1 - t0;
				++cnt;
			}
		}
	}

	return samp;
}

static void MiniMapRandomlySupersampled(int y)
{
	int x, i;
	float *p = &minimap.data1f[y * minimap.width];
	float ymin = minimap.mins[1] + minimap.size[1] * (y / (float) minimap.height);
	float dx   =                   minimap.size[0]      / (float) minimap.width;
	float dy   =                   minimap.size[1]      / (float) minimap.height;

	for(x = 0; x < minimap.width; ++x)
	{
		float xmin = minimap.mins[0] + minimap.size[0] * (x / (float) minimap.width);
		float val = 0;

		for(i = 0; i < minimap.samples; ++i)
		{
			float thisval = MiniMapSample(
				xmin + (2 * Random() - 0.5) * dx, /* exaggerated random pattern for better results */
				ymin + (2 * Random() - 0.5) * dy  /* exaggerated random pattern for better results */
			);
			val += thisval;
		}
		val /= minimap.samples * minimap.size[2];
		*p++ = val;
	}
}

static void MiniMapSupersampled(int y)
{
	int x, i;
	float *p = &minimap.data1f[y * minimap.width];
	float ymin = minimap.mins[1] + minimap.size[1] * (y / (float) minimap.height);
	float dx   =                   minimap.size[0]      / (float) minimap.width;
	float dy   =                   minimap.size[1]      / (float) minimap.height;

	for(x = 0; x < minimap.width; ++x)
	{
		float xmin = minimap.mins[0] + minimap.size[0] * (x / (float) minimap.width);
		float val = 0;

		for(i = 0; i < minimap.samples; ++i)
		{
			float thisval = MiniMapSample(
				xmin + minimap.sample_offsets[2*i+0] * dx,
				ymin + minimap.sample_offsets[2*i+1] * dy
			);
			val += thisval;
		}
		val /= minimap.samples * minimap.size[2];
		*p++ = val;
	}
}

static void MiniMapNoSupersampling(int y)
{
	int x;
	float *p = &minimap.data1f[y * minimap.width];
	float ymin = minimap.mins[1] + minimap.size[1] * (y / (float) minimap.height);

	for(x = 0; x < minimap.width; ++x)
	{
		float xmin = minimap.mins[0] + minimap.size[0] * (x / (float) minimap.width);
		*p++ = MiniMapSample(xmin, ymin) / minimap.size[2];
	}
}

static void MiniMapSharpen(int y)
{
	int x;
	qboolean up = (y > 0);
	qboolean down = (y < minimap.height - 1);
	float *p = &minimap.data1f[y * minimap.width];
	float *q = &minimap.sharpendata1f[y * minimap.width];

	for(x = 0; x < minimap.width; ++x)
	{
		qboolean left = (x > 0);
		qboolean right = (x < minimap.width - 1);
		float val = p[0] * minimap.sharpen_centermult;

		if(left && up)
			val += p[-1 -minimap.width] * minimap.sharpen_boxmult;
		if(left && down)
			val += p[-1 +minimap.width] * minimap.sharpen_boxmult;
		if(right && up)
			val += p[+1 -minimap.width] * minimap.sharpen_boxmult;
		if(right && down)
			val += p[+1 +minimap.width] * minimap.sharpen_boxmult;
			
		if(left)
			val += p[-1] * minimap.sharpen_boxmult;
		if(right)
			val += p[+1] * minimap.sharpen_boxmult;
		if(up)
			val += p[-minimap.width] * minimap.sharpen_boxmult;
		if(down)
			val += p[+minimap.width] * minimap.sharpen_boxmult;

		++p;
		*q++ = val;
	}
}

void MiniMapMakeMinsMaxs()
{
	vec3_t mins, maxs, extend;
	VectorCopy(minimap.model->mins, mins);
	VectorCopy(minimap.model->maxs, maxs);

	// line compatible to nexuiz mapinfo
	Sys_Printf("size %f %f %f %f %f %f\n", mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]);

	VectorSubtract(maxs, mins, extend);

	if(extend[1] > extend[0])
	{
		mins[0] -= (extend[1] - extend[0]) * 0.5;
		maxs[0] += (extend[1] - extend[0]) * 0.5;
	}
	else
	{
		mins[1] -= (extend[0] - extend[1]) * 0.5;
		maxs[1] += (extend[0] - extend[1]) * 0.5;
	}

	VectorSubtract(maxs, mins, extend);
	VectorScale(extend, 1.0 / 64.0, extend);

	VectorSubtract(mins, extend, mins);
	VectorAdd(maxs, extend, maxs);

	VectorCopy(mins, minimap.mins);
	VectorSubtract(maxs, mins, minimap.size);

	// line compatible to nexuiz mapinfo
	Sys_Printf("size_texcoords %f %f %f %f %f %f\n", mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]);
}

int MiniMapBSPMain( int argc, char **argv )
{
	char minimapFilename[1024];
	char basename[1024];
	char path[1024];
	char parentpath[1024];
	float minimapSharpen;
	byte *data3b, *p;
	float *q;
	int x, y;
	int i;

	/* arg checking */
	if( argc < 2 )
	{
		Sys_Printf( "Usage: q3map [-v] -minimap [-size n] [-sharpen f] [-samples n | -random n] [-o filename.tga] [-minmax Xmin Ymin Zmin Xmax Ymax Zmax] <mapname>\n" );
		return 0;
	}

	/* load the BSP first */
	strcpy( source, ExpandArg( argv[ argc - 1 ] ) );
	StripExtension( source );
	DefaultExtension( source, ".bsp" );
	Sys_Printf( "Loading %s\n", source );
	LoadBSPFile( source );

	minimap.model = &bspModels[0];
	MiniMapMakeMinsMaxs();

	*minimapFilename = 0;
	minimapSharpen = 1;
	minimap.width = minimap.height = 512;
	minimap.samples = 1;
	minimap.sample_offsets = NULL;

	/* process arguments */
	for( i = 1; i < (argc - 1); i++ )
	{
		if( !strcmp( argv[ i ],  "-size" ) )
 		{
			minimap.width = minimap.height = atoi(argv[i + 1]);
			i++;
			Sys_Printf( "Image size set to %i\n", minimap.width );
 		}
		else if( !strcmp( argv[ i ],  "-sharpen" ) )
 		{
			minimapSharpen = atof(argv[i + 1]);
			i++;
			Sys_Printf( "Sharpening coefficient set to %f\n", minimapSharpen );
 		}
		else if( !strcmp( argv[ i ],  "-samples" ) )
 		{
			minimap.samples = atoi(argv[i + 1]);
			i++;
			Sys_Printf( "Samples set to %i\n", minimap.samples );
			if(minimap.sample_offsets)
				free(minimap.sample_offsets);
			minimap.sample_offsets = malloc(2 * sizeof(*minimap.sample_offsets) * minimap.samples);
			/* TODO use a better pattern */
			for(x = 0; x < 2 * minimap.samples; ++x)
				minimap.sample_offsets[x] = Random();
 		}
		else if( !strcmp( argv[ i ],  "-random" ) )
 		{
			minimap.samples = atoi(argv[i + 1]);
			i++;
			Sys_Printf( "Random samples set to %i\n", minimap.samples );
			if(minimap.sample_offsets)
				free(minimap.sample_offsets);
			minimap.sample_offsets = NULL;
 		}
		else if( !strcmp( argv[ i ],  "-o" ) )
 		{
			strcpy(minimapFilename, argv[i + 1]);
			i++;
			Sys_Printf( "Output file name set to %s\n", minimapFilename );
 		}
		else if( !strcmp( argv[ i ],  "-minmax" ) && i < (argc - 7) )
 		{
			minimap.mins[0] = atof(argv[i + 1]);
			minimap.mins[1] = atof(argv[i + 2]);
			minimap.mins[2] = atof(argv[i + 3]);
			minimap.size[0] = atof(argv[i + 4]) - minimap.mins[0];
			minimap.size[1] = atof(argv[i + 5]) - minimap.mins[1];
			minimap.size[2] = atof(argv[i + 6]) - minimap.mins[2];
			i += 6;
			Sys_Printf( "Map mins/maxs overridden\n" );
 		}
	}

	if(!*minimapFilename)
	{
		ExtractFileBase(source, basename);
		ExtractFilePath(source, path);
		if(*path)
			path[strlen(path)-1] = 0;
		ExtractFilePath(path, parentpath);
		sprintf(minimapFilename, "%sgfx", parentpath);
		Q_mkdir(minimapFilename);
		sprintf(minimapFilename, "%sgfx/%s_mini.tga", parentpath, basename);
		Sys_Printf("Output file name automatically set to %s\n", minimapFilename);
	}

	if(minimapSharpen >= 0)
	{
		minimap.sharpen_centermult = 8 * minimapSharpen + 1;
		minimap.sharpen_boxmult    =    -minimapSharpen;
	}

	minimap.data1f = safe_malloc(minimap.width * minimap.height * sizeof(*minimap.data1f));
	data3b = safe_malloc(minimap.width * minimap.height * 3);
	if(minimapSharpen >= 0)
		minimap.sharpendata1f = safe_malloc(minimap.width * minimap.height * sizeof(*minimap.data1f));

	SetupBrushes();

	if(minimap.samples <= 1)
	{
		Sys_Printf( "\n--- MiniMapNoSupersampling (%d) ---\n", minimap.height );
		RunThreadsOnIndividual(minimap.height, qtrue, MiniMapNoSupersampling);
	}
	else
	{
		if(minimap.sample_offsets)
		{
			Sys_Printf( "\n--- MiniMapSupersampled (%d) ---\n", minimap.height );
			RunThreadsOnIndividual(minimap.height, qtrue, MiniMapSupersampled);
		}
		else
		{
			Sys_Printf( "\n--- MiniMapRandomlySupersampled (%d) ---\n", minimap.height );
			RunThreadsOnIndividual(minimap.height, qtrue, MiniMapRandomlySupersampled);
		}
	}

	if(minimap.sharpendata1f)
	{
		Sys_Printf( "\n--- MiniMapSharpen (%d) ---\n", minimap.height );
		RunThreadsOnIndividual(minimap.height, qtrue, MiniMapSharpen);
		q = minimap.sharpendata1f;
	}
	else
	{
		q = minimap.data1f;
	}

	Sys_Printf( "\nConverting...");
	p = data3b;
	for(y = 0; y < minimap.height; ++y)
		for(x = 0; x < minimap.width; ++x)
		{
			byte b;
			float v = *q++;
			if(v < 0) v = 0;
			if(v > 255.0/256.0) v = 255.0/256.0;
			b = v * 256;
			*p++ = b;
			*p++ = b;
			*p++ = b;
		}

	Sys_Printf( " writing to %s...", minimapFilename );
	WriteTGA24(minimapFilename, data3b, minimap.width, minimap.height, qfalse);

	Sys_Printf( " done.\n" );

	/* return to sender */
	return 0;
}





/*
MD4BlockChecksum()
calculates an md4 checksum for a block of data
*/

static int MD4BlockChecksum( void *buffer, int length )
{
	return Com_BlockChecksum(buffer, length);
}

/*
FixAAS()
resets an aas checksum to match the given BSP
*/

int FixAAS( int argc, char **argv )
{
	int			length, checksum;
	void		*buffer;
	FILE		*file;
	char		aas[ 1024 ], **ext;
	char		*exts[] =
				{
					".aas",
					"_b0.aas",
					"_b1.aas",
					NULL
				};
	
	
	/* arg checking */
	if( argc < 2 )
	{
		Sys_Printf( "Usage: q3map -fixaas [-v] <mapname>\n" );
		return 0;
	}
	
	/* do some path mangling */
	strcpy( source, ExpandArg( argv[ argc - 1 ] ) );
	StripExtension( source );
	DefaultExtension( source, ".bsp" );
	
	/* note it */
	Sys_Printf( "--- FixAAS ---\n" );
	
	/* load the bsp */
	Sys_Printf( "Loading %s\n", source );
	length = LoadFile( source, &buffer );
	
	/* create bsp checksum */
	Sys_Printf( "Creating checksum...\n" );
	checksum = LittleLong( MD4BlockChecksum( buffer, length ) );
	
	/* write checksum to aas */
	ext = exts;
	while( *ext )
	{
		/* mangle name */
		strcpy( aas, source );
		StripExtension( aas );
		strcat( aas, *ext );
		Sys_Printf( "Trying %s\n", aas );
		ext++;
		
		/* fix it */
		file = fopen( aas, "r+b" );
		if( !file )
			continue;
		if( fwrite( &checksum, 4, 1, file ) != 1 )
			Error( "Error writing checksum to %s", aas );
		fclose( file );
	}
	
	/* return to sender */
	return 0;
}



/*
AnalyzeBSP() - ydnar
analyzes a Quake engine BSP file
*/

typedef struct abspHeader_s
{
	char			ident[ 4 ];
	int				version;
	
	bspLump_t		lumps[ 1 ];	/* unknown size */
}
abspHeader_t;

typedef struct abspLumpTest_s
{
	int				radix, minCount;
	char			*name;
}
abspLumpTest_t;

int AnalyzeBSP( int argc, char **argv )
{
	abspHeader_t			*header;
	int						size, i, version, offset, length, lumpInt, count;
	char					ident[ 5 ];
	void					*lump;
	float					lumpFloat;
	char					lumpString[ 1024 ], source[ 1024 ];
	qboolean				lumpSwap = qfalse;
	abspLumpTest_t			*lumpTest;
	static abspLumpTest_t	lumpTests[] =
							{
								{ sizeof( bspPlane_t ),			6,		"IBSP LUMP_PLANES" },
								{ sizeof( bspBrush_t ),			1,		"IBSP LUMP_BRUSHES" },
								{ 8,							6,		"IBSP LUMP_BRUSHSIDES" },
								{ sizeof( bspBrushSide_t ),		6,		"RBSP LUMP_BRUSHSIDES" },
								{ sizeof( bspModel_t ),			1,		"IBSP LUMP_MODELS" },
								{ sizeof( bspNode_t ),			2,		"IBSP LUMP_NODES" },
								{ sizeof( bspLeaf_t ),			1,		"IBSP LUMP_LEAFS" },
								{ 104,							3,		"IBSP LUMP_DRAWSURFS" },
								{ 44,							3,		"IBSP LUMP_DRAWVERTS" },
								{ 4,							6,		"IBSP LUMP_DRAWINDEXES" },
								{ 128 * 128 * 3,				1,		"IBSP LUMP_LIGHTMAPS" },
								{ 256 * 256 * 3,				1,		"IBSP LUMP_LIGHTMAPS (256 x 256)" },
								{ 512 * 512 * 3,				1,		"IBSP LUMP_LIGHTMAPS (512 x 512)" },
								{ 0, 0, NULL }
							};
	
	
	/* arg checking */
	if( argc < 1 )
	{
		Sys_Printf( "Usage: q3map -analyze [-lumpswap] [-v] <mapname>\n" );
		return 0;
	}
	
	/* process arguments */
	for( i = 1; i < (argc - 1); i++ )
	{
		/* -format map|ase|... */
		if( !strcmp( argv[ i ],  "-lumpswap" ) )
		{
			Sys_Printf( "Swapped lump structs enabled\n" );
 			lumpSwap = qtrue;
 		}
	}
	
	/* clean up map name */
	strcpy( source, ExpandArg( argv[ i ] ) );
	Sys_Printf( "Loading %s\n", source );
	
	/* load the file */
	size = LoadFile( source, (void**) &header );
	if( size == 0 || header == NULL )
	{
		Sys_Printf( "Unable to load %s.\n", source );
		return -1;
	}
	
	/* analyze ident/version */
	memcpy( ident, header->ident, 4 );
	ident[ 4 ] = '\0';
	version = LittleLong( header->version );
	
	Sys_Printf( "Identity:      %s\n", ident );
	Sys_Printf( "Version:       %d\n", version );
	Sys_Printf( "---------------------------------------\n" );
	
	/* analyze each lump */
	for( i = 0; i < 100; i++ )
	{
		/* call of duty swapped lump pairs */
		if( lumpSwap )
		{
			offset = LittleLong( header->lumps[ i ].length );
			length = LittleLong( header->lumps[ i ].offset );
		}
		
		/* standard lump pairs */
		else
		{
			offset = LittleLong( header->lumps[ i ].offset );
			length = LittleLong( header->lumps[ i ].length );
		}
		
		/* extract data */
		lump = (byte*) header + offset;
		lumpInt = LittleLong( (int) *((int*) lump) );
		lumpFloat = LittleFloat( (float) *((float*) lump) );
		memcpy( lumpString, (char*) lump, (length < 1024 ? length : 1024) );
		lumpString[ 1024 ] = '\0';
		
		/* print basic lump info */
		Sys_Printf( "Lump:          %d\n", i );
		Sys_Printf( "Offset:        %d bytes\n", offset );
		Sys_Printf( "Length:        %d bytes\n", length );
		
		/* only operate on valid lumps */
		if( length > 0 )
		{
			/* print data in 4 formats */
			Sys_Printf( "As hex:        %08X\n", lumpInt );
			Sys_Printf( "As int:        %d\n", lumpInt );
			Sys_Printf( "As float:      %f\n", lumpFloat );
			Sys_Printf( "As string:     %s\n", lumpString );
			
			/* guess lump type */
			if( lumpString[ 0 ] == '{' && lumpString[ 2 ] == '"' )
				Sys_Printf( "Type guess:    IBSP LUMP_ENTITIES\n" );
			else if( strstr( lumpString, "textures/" ) )
				Sys_Printf( "Type guess:    IBSP LUMP_SHADERS\n" );
			else
			{
				/* guess based on size/count */
				for( lumpTest = lumpTests; lumpTest->radix > 0; lumpTest++ )
				{
					if( (length % lumpTest->radix) != 0 )
						continue;
					count = length / lumpTest->radix;
					if( count < lumpTest->minCount )
						continue;
					Sys_Printf( "Type guess:    %s (%d x %d)\n", lumpTest->name, count, lumpTest->radix );
				}
			}
		}
		
		Sys_Printf( "---------------------------------------\n" );
		
		/* end of file */
		if( offset + length >= size )
			break;
	}
	
	/* last stats */
	Sys_Printf( "Lump count:    %d\n", i + 1 );
	Sys_Printf( "File size:     %d bytes\n", size );
	
	/* return to caller */
	return 0;
}



/*
BSPInfo()
emits statistics about the bsp file
*/

int BSPInfo( int count, char **fileNames )
{
	int			i;
	char		source[ 1024 ], ext[ 64 ];
	int			size;
	FILE		*f;
	
	
	/* dummy check */
	if( count < 1 )
	{
		Sys_Printf( "No files to dump info for.\n");
		return -1;
	}
	
	/* enable info mode */
	infoMode = qtrue;
	
	/* walk file list */
	for( i = 0; i < count; i++ )
	{
		Sys_Printf( "---------------------------------\n" );
		
		/* mangle filename and get size */
		strcpy( source, fileNames[ i ] );
		ExtractFileExtension( source, ext );
		if( !Q_stricmp( ext, "map" ) )
			StripExtension( source );
		DefaultExtension( source, ".bsp" );
		f = fopen( source, "rb" );
		if( f )
		{
			size = Q_filelength (f);
			fclose( f );
		}
		else
			size = 0;
		
		/* load the bsp file and print lump sizes */
		Sys_Printf( "%s\n", source );
		LoadBSPFile( source );		
		PrintBSPFileSizes();
		
		/* print sizes */
		Sys_Printf( "\n" );
		Sys_Printf( "          total         %9d\n", size );
		Sys_Printf( "                        %9d KB\n", size / 1024 );
		Sys_Printf( "                        %9d MB\n", size / (1024 * 1024) );
		
		Sys_Printf( "---------------------------------\n" );
	}
	
	/* return count */
	return i;
}


static void ExtrapolateTexcoords(const float *axyz, const float *ast, const float *bxyz, const float *bst, const float *cxyz, const float *cst, const float *axyz_new, float *ast_out, const float *bxyz_new, float *bst_out, const float *cxyz_new, float *cst_out)
{
	vec4_t scoeffs, tcoeffs;
	float md;
	m4x4_t solvematrix;

	vec3_t norm;
	vec3_t dab, dac;
	VectorSubtract(bxyz, axyz, dab);
	VectorSubtract(cxyz, axyz, dac);
	CrossProduct(dab, dac, norm);
	
	// assume:
	//   s = f(x, y, z)
	//   s(v + norm) = s(v) when n ortho xyz
	
	// s(v) = DotProduct(v, scoeffs) + scoeffs[3]

	// solve:
	//   scoeffs * (axyz, 1) == ast[0]
	//   scoeffs * (bxyz, 1) == bst[0]
	//   scoeffs * (cxyz, 1) == cst[0]
	//   scoeffs * (norm, 0) == 0
	// scoeffs * [axyz, 1 | bxyz, 1 | cxyz, 1 | norm, 0] = [ast[0], bst[0], cst[0], 0]
	solvematrix[0] = axyz[0];
	solvematrix[4] = axyz[1];
	solvematrix[8] = axyz[2];
	solvematrix[12] = 1;
	solvematrix[1] = bxyz[0];
	solvematrix[5] = bxyz[1];
	solvematrix[9] = bxyz[2];
	solvematrix[13] = 1;
	solvematrix[2] = cxyz[0];
	solvematrix[6] = cxyz[1];
	solvematrix[10] = cxyz[2];
	solvematrix[14] = 1;
	solvematrix[3] = norm[0];
	solvematrix[7] = norm[1];
	solvematrix[11] = norm[2];
	solvematrix[15] = 0;

	md = m4_det(solvematrix);
	if(md*md < 1e-10)
	{
		Sys_Printf("Cannot invert some matrix, some texcoords aren't extrapolated!");
		return;
	}

	m4x4_invert(solvematrix);

	scoeffs[0] = ast[0];
	scoeffs[1] = bst[0];
	scoeffs[2] = cst[0];
	scoeffs[3] = 0;
	m4x4_transform_vec4(solvematrix, scoeffs);
	tcoeffs[0] = ast[1];
	tcoeffs[1] = bst[1];
	tcoeffs[2] = cst[1];
	tcoeffs[3] = 0;
	m4x4_transform_vec4(solvematrix, tcoeffs);

	ast_out[0] = scoeffs[0] * axyz_new[0] + scoeffs[1] * axyz_new[1] + scoeffs[2] * axyz_new[2] + scoeffs[3];
	ast_out[1] = tcoeffs[0] * axyz_new[0] + tcoeffs[1] * axyz_new[1] + tcoeffs[2] * axyz_new[2] + tcoeffs[3];
	bst_out[0] = scoeffs[0] * bxyz_new[0] + scoeffs[1] * bxyz_new[1] + scoeffs[2] * bxyz_new[2] + scoeffs[3];
	bst_out[1] = tcoeffs[0] * bxyz_new[0] + tcoeffs[1] * bxyz_new[1] + tcoeffs[2] * bxyz_new[2] + tcoeffs[3];
	cst_out[0] = scoeffs[0] * cxyz_new[0] + scoeffs[1] * cxyz_new[1] + scoeffs[2] * cxyz_new[2] + scoeffs[3];
	cst_out[1] = tcoeffs[0] * cxyz_new[0] + tcoeffs[1] * cxyz_new[1] + tcoeffs[2] * cxyz_new[2] + tcoeffs[3];
}

/*
ScaleBSPMain()
amaze and confuse your enemies with wierd scaled maps!
*/

int ScaleBSPMain( int argc, char **argv )
{
	int			i, j;
	float		f, a;
	vec3_t scale;
	vec3_t		vec;
	char		str[ 1024 ];
	int uniform, axis;
	qboolean texscale;
	float *old_xyzst = NULL;
	
	
	/* arg checking */
	if( argc < 3 )
	{
		Sys_Printf( "Usage: q3map [-v] -scale [-tex] <value> <mapname>\n" );
		return 0;
	}
	
	/* get scale */
	scale[2] = scale[1] = scale[0] = atof( argv[ argc - 2 ] );
	if(argc >= 4)
		scale[1] = scale[0] = atof( argv[ argc - 3 ] );
	if(argc >= 5)
		scale[0] = atof( argv[ argc - 4 ] );

	texscale = !strcmp(argv[1], "-tex");
	
	uniform = ((scale[0] == scale[1]) && (scale[1] == scale[2]));

	if( scale[0] == 0.0f || scale[1] == 0.0f || scale[2] == 0.0f )
	{
		Sys_Printf( "Usage: q3map [-v] -scale [-tex] <value> <mapname>\n" );
		Sys_Printf( "Non-zero scale value required.\n" );
		return 0;
	}
	
	/* do some path mangling */
	strcpy( source, ExpandArg( argv[ argc - 1 ] ) );
	StripExtension( source );
	DefaultExtension( source, ".bsp" );
	
	/* load the bsp */
	Sys_Printf( "Loading %s\n", source );
	LoadBSPFile( source );
	ParseEntities();
	
	/* note it */
	Sys_Printf( "--- ScaleBSP ---\n" );
	Sys_FPrintf( SYS_VRB, "%9d entities\n", numEntities );
	
	/* scale entity keys */
	for( i = 0; i < numBSPEntities && i < numEntities; i++ )
	{
		/* scale origin */
		GetVectorForKey( &entities[ i ], "origin", vec );
		if( (vec[ 0 ] || vec[ 1 ] || vec[ 2 ]) )
		{
			vec[0] *= scale[0];
			vec[1] *= scale[1];
			vec[2] *= scale[2];
			sprintf( str, "%f %f %f", vec[ 0 ], vec[ 1 ], vec[ 2 ] );
			SetKeyValue( &entities[ i ], "origin", str );
		}

		a = FloatForKey( &entities[ i ], "angle" );
		if(a == -1 || a == -2) // z scale
			axis = 2;
		else if(fabs(sin(DEG2RAD(a))) < 0.707)
			axis = 0;
		else
			axis = 1;
		
		/* scale door lip */
		f = FloatForKey( &entities[ i ], "lip" );
		if( f )
		{
			f *= scale[axis];
			sprintf( str, "%f", f );
			SetKeyValue( &entities[ i ], "lip", str );
		}
		
		/* scale plat height */
		f = FloatForKey( &entities[ i ], "height" );
		if( f )
		{
			f *= scale[2];
			sprintf( str, "%f", f );
			SetKeyValue( &entities[ i ], "height", str );
		}

		// TODO maybe allow a definition file for entities to specify which values are scaled how?
	}
	
	/* scale models */
	for( i = 0; i < numBSPModels; i++ )
	{
		bspModels[ i ].mins[0] *= scale[0];
		bspModels[ i ].mins[1] *= scale[1];
		bspModels[ i ].mins[2] *= scale[2];
		bspModels[ i ].maxs[0] *= scale[0];
		bspModels[ i ].maxs[1] *= scale[1];
		bspModels[ i ].maxs[2] *= scale[2];
	}
	
	/* scale nodes */
	for( i = 0; i < numBSPNodes; i++ )
	{
		bspNodes[ i ].mins[0] *= scale[0];
		bspNodes[ i ].mins[1] *= scale[1];
		bspNodes[ i ].mins[2] *= scale[2];
		bspNodes[ i ].maxs[0] *= scale[0];
		bspNodes[ i ].maxs[1] *= scale[1];
		bspNodes[ i ].maxs[2] *= scale[2];
	}
	
	/* scale leafs */
	for( i = 0; i < numBSPLeafs; i++ )
	{
		bspLeafs[ i ].mins[0] *= scale[0];
		bspLeafs[ i ].mins[1] *= scale[1];
		bspLeafs[ i ].mins[2] *= scale[2];
		bspLeafs[ i ].maxs[0] *= scale[0];
		bspLeafs[ i ].maxs[1] *= scale[1];
		bspLeafs[ i ].maxs[2] *= scale[2];
	}
	
	if(texscale)
	{
		Sys_Printf("Using texture unlocking (and probably breaking texture alignment a lot)\n");
		old_xyzst = safe_malloc(sizeof(*old_xyzst) * numBSPDrawVerts * 5);
		for(i = 0; i < numBSPDrawVerts; i++)
		{
			old_xyzst[5*i+0] = bspDrawVerts[i].xyz[0];
			old_xyzst[5*i+1] = bspDrawVerts[i].xyz[1];
			old_xyzst[5*i+2] = bspDrawVerts[i].xyz[2];
			old_xyzst[5*i+3] = bspDrawVerts[i].st[0];
			old_xyzst[5*i+4] = bspDrawVerts[i].st[1];
		}
	}

	/* scale drawverts */
	for( i = 0; i < numBSPDrawVerts; i++ )
	{
		bspDrawVerts[i].xyz[0] *= scale[0];
		bspDrawVerts[i].xyz[1] *= scale[1];
		bspDrawVerts[i].xyz[2] *= scale[2];
		bspDrawVerts[i].normal[0] /= scale[0];
		bspDrawVerts[i].normal[1] /= scale[1];
		bspDrawVerts[i].normal[2] /= scale[2];
		VectorNormalize(bspDrawVerts[i].normal, bspDrawVerts[i].normal);
	}

	if(texscale)
	{
		for(i = 0; i < numBSPDrawSurfaces; i++)
		{
			switch(bspDrawSurfaces[i].surfaceType)
			{
				case SURFACE_FACE:
				case SURFACE_META:
					if(bspDrawSurfaces[i].numIndexes % 3)
						Error("Not a triangulation!");
					for(j = bspDrawSurfaces[i].firstIndex; j < bspDrawSurfaces[i].firstIndex + bspDrawSurfaces[i].numIndexes; j += 3)
					{
						int ia = bspDrawIndexes[j] + bspDrawSurfaces[i].firstVert, ib = bspDrawIndexes[j+1] + bspDrawSurfaces[i].firstVert, ic = bspDrawIndexes[j+2] + bspDrawSurfaces[i].firstVert;
						bspDrawVert_t *a = &bspDrawVerts[ia], *b = &bspDrawVerts[ib], *c = &bspDrawVerts[ic];
						float *oa = &old_xyzst[ia*5], *ob = &old_xyzst[ib*5], *oc = &old_xyzst[ic*5];
						// extrapolate:
						//   a->xyz -> oa
						//   b->xyz -> ob
						//   c->xyz -> oc
						ExtrapolateTexcoords(
							&oa[0], &oa[3],
							&ob[0], &ob[3],
							&oc[0], &oc[3],
							a->xyz, a->st,
							b->xyz, b->st,
							c->xyz, c->st);
					}
					break;
			}
		}
	}
	
	/* scale planes */
	if(uniform)
	{
		for( i = 0; i < numBSPPlanes; i++ )
		{
			bspPlanes[ i ].dist *= scale[0];
		}
	}
	else
	{
		for( i = 0; i < numBSPPlanes; i++ )
		{
			bspPlanes[ i ].normal[0] /= scale[0];
			bspPlanes[ i ].normal[1] /= scale[1];
			bspPlanes[ i ].normal[2] /= scale[2];
			f = 1/VectorLength(bspPlanes[i].normal);
			VectorScale(bspPlanes[i].normal, f, bspPlanes[i].normal);
			bspPlanes[ i ].dist *= f;
		}
	}
	
	/* scale gridsize */
	GetVectorForKey( &entities[ 0 ], "gridsize", vec );
	if( (vec[ 0 ] + vec[ 1 ] + vec[ 2 ]) == 0.0f )
		VectorCopy( gridSize, vec );
	vec[0] *= scale[0];
	vec[1] *= scale[1];
	vec[2] *= scale[2];
	sprintf( str, "%f %f %f", vec[ 0 ], vec[ 1 ], vec[ 2 ] );
	SetKeyValue( &entities[ 0 ], "gridsize", str );

	/* inject command line parameters */
	InjectCommandLine(argv, 0, argc - 1);
	
	/* write the bsp */
	UnparseEntities();
	StripExtension( source );
	DefaultExtension( source, "_s.bsp" );
	Sys_Printf( "Writing %s\n", source );
	WriteBSPFile( source );
	
	/* return to sender */
	return 0;
}



/*
ConvertBSPMain()
main argument processing function for bsp conversion
*/

int ConvertBSPMain( int argc, char **argv )
{
	int		i;
	int		(*convertFunc)( char * );
	game_t	*convertGame;
	
	
	/* set default */
	convertFunc = ConvertBSPToASE;
	convertGame = NULL;
	
	/* arg checking */
	if( argc < 1 )
	{
		Sys_Printf( "Usage: q3map -scale <value> [-v] <mapname>\n" );
		return 0;
	}
	
	/* process arguments */
	for( i = 1; i < (argc - 1); i++ )
	{
		/* -format map|ase|... */
		if( !strcmp( argv[ i ],  "-format" ) )
 		{
			i++;
			if( !Q_stricmp( argv[ i ], "ase" ) )
				convertFunc = ConvertBSPToASE;
			else if( !Q_stricmp( argv[ i ], "map" ) )
				convertFunc = ConvertBSPToMap;
			else
			{
				convertGame = GetGame( argv[ i ] );
				if( convertGame == NULL )
					Sys_Printf( "Unknown conversion format \"%s\". Defaulting to ASE.\n", argv[ i ] );
			}
 		}
		else if( !strcmp( argv[ i ],  "-ne" ) )
 		{
			normalEpsilon = atof( argv[ i + 1 ] );
 			i++;
			Sys_Printf( "Normal epsilon set to %f\n", normalEpsilon );
 		}
		else if( !strcmp( argv[ i ],  "-de" ) )
 		{
			distanceEpsilon = atof( argv[ i + 1 ] );
 			i++;
			Sys_Printf( "Distance epsilon set to %f\n", distanceEpsilon );
 		}
		else if( !strcmp( argv[ i ],  "-shadersasbitmap" ) )
			shadersAsBitmap = qtrue;
	}
	
	/* clean up map name */
	strcpy( source, ExpandArg( argv[ i ] ) );
	StripExtension( source );
	DefaultExtension( source, ".bsp" );
	
	LoadShaderInfo();
	
	Sys_Printf( "Loading %s\n", source );
	
	/* ydnar: load surface file */
	//%	LoadSurfaceExtraFile( source );
	
	LoadBSPFile( source );
	
	/* parse bsp entities */
	ParseEntities();
	
	/* bsp format convert? */
	if( convertGame != NULL )
	{
		/* set global game */
		game = convertGame;
		
		/* write bsp */
		StripExtension( source );
		DefaultExtension( source, "_c.bsp" );
		Sys_Printf( "Writing %s\n", source );
		WriteBSPFile( source );
		
		/* return to sender */
		return 0;
	}
	
	/* normal convert */
	return convertFunc( source );
}



/*
main()
q3map mojo...
*/

int main( int argc, char **argv )
{
	int		i, r;
	double	start, end;
	
	
	/* we want consistent 'randomness' */
	srand( 0 );
	
	/* start timer */
	start = I_FloatTime();

	/* this was changed to emit version number over the network */
	printf( Q3MAP_VERSION "\n" );
	
	/* set exit call */
	atexit( ExitQ3Map );

	/* read general options first */
	for( i = 1; i < argc; i++ )
	{
		/* -connect */
		if( !strcmp( argv[ i ], "-connect" ) )
		{
			argv[ i ] = NULL;
			i++;
			Broadcast_Setup( argv[ i ] );
			argv[ i ] = NULL;
		}
		
		/* verbose */
		else if( !strcmp( argv[ i ], "-v" ) )
		{
			if(!verbose)
			{
				verbose = qtrue;
				argv[ i ] = NULL;
			}
		}
		
		/* force */
		else if( !strcmp( argv[ i ], "-force" ) )
		{
			force = qtrue;
			argv[ i ] = NULL;
		}
		
		/* patch subdivisions */
		else if( !strcmp( argv[ i ], "-subdivisions" ) )
		{
			argv[ i ] = NULL;
			i++;
			patchSubdivisions = atoi( argv[ i ] );
			argv[ i ] = NULL;
			if( patchSubdivisions <= 0 )
				patchSubdivisions = 1;
		}
		
		/* threads */
		else if( !strcmp( argv[ i ], "-threads" ) )
		{
			argv[ i ] = NULL;
			i++;
			numthreads = atoi( argv[ i ] );
			argv[ i ] = NULL;
		}
	}
	
	/* init model library */
	PicoInit();
	PicoSetMallocFunc( safe_malloc );
	PicoSetFreeFunc( free );
	PicoSetPrintFunc( PicoPrintFunc );
	PicoSetLoadFileFunc( PicoLoadFileFunc );
	PicoSetFreeFileFunc( free );
	
	/* set number of threads */
	ThreadSetDefault();
	
	/* generate sinusoid jitter table */
	for( i = 0; i < MAX_JITTERS; i++ )
	{
		jitters[ i ] = sin( i * 139.54152147 );
		//%	Sys_Printf( "Jitter %4d: %f\n", i, jitters[ i ] );
	}
	
	/* we print out two versions, q3map's main version (since it evolves a bit out of GtkRadiant)
	   and we put the GtkRadiant version to make it easy to track with what version of Radiant it was built with */
	
	Sys_Printf( "Q3Map         - v1.0r (c) 1999 Id Software Inc.\n" );
	Sys_Printf( "Q3Map (ydnar) - v" Q3MAP_VERSION "\n" );
	Sys_Printf( "NetRadiant    - v" RADIANT_VERSION " " __DATE__ " " __TIME__ "\n" );
	Sys_Printf( "%s\n", Q3MAP_MOTD );
	
	/* ydnar: new path initialization */
	InitPaths( &argc, argv );

	/* set game options */
	if (!patchSubdivisions)
		patchSubdivisions = game->patchSubdivisions;
	
	/* check if we have enough options left to attempt something */
	if( argc < 2 )
		Error( "Usage: %s [general options] [options] mapfile", argv[ 0 ] );
	
	/* fixaas */
	if( !strcmp( argv[ 1 ], "-fixaas" ) )
		r = FixAAS( argc - 1, argv + 1 );
	
	/* analyze */
	else if( !strcmp( argv[ 1 ], "-analyze" ) )
		r = AnalyzeBSP( argc - 1, argv + 1 );
	
	/* info */
	else if( !strcmp( argv[ 1 ], "-info" ) )
		r = BSPInfo( argc - 2, argv + 2 );
	
	/* vis */
	else if( !strcmp( argv[ 1 ], "-vis" ) )
		r = VisMain( argc - 1, argv + 1 );
	
	/* light */
	else if( !strcmp( argv[ 1 ], "-light" ) )
		r = LightMain( argc - 1, argv + 1 );
	
	/* vlight */
	else if( !strcmp( argv[ 1 ], "-vlight" ) )
	{
		Sys_Printf( "WARNING: VLight is no longer supported, defaulting to -light -fast instead\n\n" );
		argv[ 1 ] = "-fast";	/* eek a hack */
		r = LightMain( argc, argv );
	}
	
	/* ydnar: lightmap export */
	else if( !strcmp( argv[ 1 ], "-export" ) )
		r = ExportLightmapsMain( argc - 1, argv + 1 );
	
	/* ydnar: lightmap import */
	else if( !strcmp( argv[ 1 ], "-import" ) )
		r = ImportLightmapsMain( argc - 1, argv + 1 );
	
	/* ydnar: bsp scaling */
	else if( !strcmp( argv[ 1 ], "-scale" ) )
		r = ScaleBSPMain( argc - 1, argv + 1 );
	
	/* ydnar: bsp conversion */
	else if( !strcmp( argv[ 1 ], "-convert" ) )
		r = ConvertBSPMain( argc - 1, argv + 1 );
	
	/* div0: minimap */
	else if( !strcmp( argv[ 1 ], "-minimap" ) )
		r = MiniMapBSPMain(argc - 1, argv + 1);

	/* ydnar: otherwise create a bsp */
	else
		r = BSPMain( argc, argv );
	
	/* emit time */
	end = I_FloatTime();
	Sys_Printf( "%9.0f seconds elapsed\n", end - start );
	
	/* shut down connection */
	Broadcast_Shutdown();
	
	/* return any error code */
	return r;
}
