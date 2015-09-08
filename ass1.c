/*****************************************************************************
 * File: ass1.c
 * Author: Nathanael Tomlinson
 * Date: 22/08/2015
 *
 * Description:
 * CSSE2310 assignment 1 implementation.
 ****************************************************************************/

/* Includes ----------------------------------------------------------------*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>

/* Internal Macros ---------------------------------------------------------*/

/* argc range and argv indexes */
#define ARGC_MIN     4
#define ARGC_MAX     5
#define ARGV_HEIGHT  1
#define ARGV_WIDTH   2
#define ARGV_PLAYERS 3
#define ARGV_PATH    4

/* limits imposed by the spec */
#define PLAYER_MIN 2
#define DIMEN_MIN  2
#define PLAYER_MAX 100
#define DIMEN_MAX  999

/* player ID <=> player name */
#define NAME(id) ((id)   + 'A')
#define ID(name) ((name) - 'A')

/* prints the grid to stdout */
#define PGRID(gamep) (fputs((gamep)->horz.start - 1, stdout))

/* test for EOL - must accept '\n' or EOF */
#define ISEOL(c) ((c) == '\n' || (c) == EOF)

/* 
 * memcpy and memset wrappers that evaluate to pointers past the end of the
 * destination block (instead of the beginning).
 */
#define MEMPSET(dst, val, n) ((void *)((char *)memset(dst, val, n) + (n)))
#define MEMPCPY(dst, src, n) ((void *)((char *)memcpy(dst, src, n) + (n)))

/* Internal Types ----------------------------------------------------------*/

/*
 * Unrecoverable error types (passed to fatal())
 */
enum error {
	E_USAGE = 1, /* incorrect no. of cmd line args */
	E_DIMEN,     /* invalid grid dimensions        */
	E_PLAYERS,   /* invalid player count           */
	E_NOFILE,    /* cannot open grid file          */
	E_LOAD,      /* error reading grid file        */
	E_EOF,       /* EOF when user input expected   */
	E_NOMEM = 9  /* malloc returns NULL            */
};

/*
 * Possible move types (returned by getmove())
 */
enum move {
	M_NONE,  /* no/invalid move */
	M_PLACE, /* placement move  */
	M_SAVE   /* save game move  */
};

/*
 * An "access point" to the array that contains the grid. It is intended to
 * represent a single type of edge (either horizontal or vertical).
 */
struct grid {
	char *start;  /* a pointer to the first edge of this type        */
	int   height; /* the number of rows containing this type of edge */
	int   width;  /* the number of this type of edge in each row     */
	int   offset; /* pointer to edge +/- offset == centre of cell    */
	int   rep;    /* the character representation of this edge type  */
};

/*
 * Stores the current state of the game.
 */
struct game {
	/* PLAYER DATA ---------------------------------------------------*/
	long int    scores[PLAYER_MAX]; /* each player's current score    */
	long int    empty;              /* the number of empty cells left */
	int         players;            /* number of players in the game  */
	int         next;               /* the next player to move        */
	/* GRID DATA -----------------------------------------------------*/
	struct grid horz;               /* horizontal edge access point   */
	struct grid vert;               /* vertical edge access point     */
	int         cols;               /* number of characters per row   */
	/*----------------------------------------------------------------*/
};

/*
 * Contains the arguments to a place move.
 */
struct place {
	struct grid *grid; /* grid access point         */
	int          cols; /* grid columns              */
	int          name; /* the placing player's name */
	int          h;    /* row position (in grid)    */
	int          w;    /* edge position (in row)    */
};

/*
 * Contains arguments to any type of move. (Only at move one type valid at a
 * time).
 */
union args {
	struct place place;              /* place arguments */
	char         path[FILENAME_MAX]; /* save file path  */
};

/* Internal Functions Definition -------------------------------------------*/

/*
 * Prints the error message corresponding to 'err' and exits the calling
 * process.
 */
static void fatal(enum error err)
{
	static const char *const msgs[] = {
		0,
		"Usage: boxes height width playercount [filename]\n",
		"Invalid grid dimensions\n",
		"Invalid player count\n",
		"Invalid grid file\n",
		"Error reading grid contents\n",
		"End of user input\n",
		0,
		0,
		"System call failure\n"
	};

	fputs(msgs[err], stderr);
	exit(err);
}

/*
 * Reads an integer from a file steam, stopping when the converted value
 * reaches 'max' or a non-digit character is read, (whichever comes first).
 * The integer pointed to by 'end' is set to the character that stopped the
 * conversion, or EOF if the end of the stream was reached.
 *
 * RETURN VALUE:
 * -1 if no integer was read, otherwise an integer in the range [0, max].
 */
static int fgeti(FILE *f, int *endp, int max)
{
	int c, val = 0;

	c = fgetc(f);
	if (!isdigit(c)) {
		val = -1;
	} else if (c == '0') {
		c = fgetc(f);
	} else {
		do {
			val = 10 * val + c - '0';
			if (max < val) {
				val = max;
				break;
			}
			c = fgetc(f);
		} while (isdigit(c));
	}

	*endp = c;
	return val;
}

/*
 * Initialises a blank grid. 'rows' and 'cols' are the size of the grid in
 * characters.
 */
static void initgrid(char *grid, int rows, int cols)
{
	char *ptr = grid;
	char *end = grid + cols - 1;

	*ptr++ = '+';
	do {
		*ptr++ = ' ';
		*ptr++ = '+';
	} while (ptr < end);
	*ptr++ = '\n';

	end = grid + rows * cols;
	do {
		ptr = MEMPSET(ptr, ' ', cols - 1);
		*ptr++ = '\n';
		ptr = MEMPCPY(ptr, grid, cols);
	} while (ptr < end);
}

/*
 * Allocates and initialises a blank game. 'height' and 'width' are the size
 * of the gird in cells. 'players' is the number of players in the game.
 *
 * CALLS FATAL() WITH:
 * - E_NOMEM if memory allocation fails.
 */
static void newgame(struct game *game, int height, int width, int players)
{
	char *grid;
	int rows = 2 * height + 1;
	int cols = 2 * (width + 1);

	/* 2 padding rows == no explicit bounds checking later */
	grid = calloc((rows + 2) * cols, sizeof *grid);
	if (!grid)
		fatal(E_NOMEM);
	grid += cols;
	initgrid(grid, rows, cols);

	memset(game->scores, 0, players * sizeof *game->scores);
	game->empty   = width * height;
	game->players = players;
	game->next    = 0;

	game->horz.start  = grid + 1;
	game->horz.height = height + 1;
	game->horz.width  = width;
	game->horz.offset = cols;
	game->horz.rep    = '-';

	game->vert.start  = grid + cols;
	game->vert.height = height;
	game->vert.width  = width + 1;
	game->vert.offset = 1;
	game->vert.rep    = '|';

	game->cols = cols;
}

/*
 * Updates the grid row pointed to by 'grid' with edge data read from
 * 'f'. The grid must have been initialised previously. 'rep' is used
 * as the edge representation (should be '-' or '|').
 *
 * RETURN VALUE:
 * returns a pointer to the first edge in the next row.
 *
 * CALLS FATAL() WITH:
 * - E_LOAD if 'f' doesn't contain valid edge data.
 */
static char *loadedges(FILE *f, char *grid, int rep)
{
	while (*grid == ' ') {
		switch (fgetc(f)) {
		case '1':
			*grid = rep;
		case '0':
			grid += 2;
			break;
		default:
			fatal(E_LOAD);
		}
	}

	if (fgetc(f) != '\n')
		fatal(E_LOAD);

	return grid + 1;
}

/*
 * Opens the grid file at 'path' and loads it into an existing blank game.
 *
 * CALLS FATAL() WITH:
 * - E_NOFILE if the grid file cannot be opened.
 * - E_LOAD   if the grid file format is invalid.
 */
static void loadgame(struct game *game, const char *path)
{
	FILE *f;
	char *ptr;
	int end, id;

	f = fopen(path, "r");
	if (!f)
		fatal(E_NOFILE);

	id = fgeti(f, &end, game->players) - 1;
	if (id < 0 || end != '\n')
		fatal(E_LOAD);
	game->next = id;

	ptr = loadedges(f, game->horz.start, '-');
	do {
		ptr = loadedges(f, ptr, '|');
		ptr = loadedges(f, ptr, '-');
	} while (*ptr);

	ptr = game->vert.start + 1;
	while (*ptr) {
		id = fgeti(f, &end, game->players) - 1;
		if (id >= 0) {
			*ptr = NAME(id);
			++game->scores[id];
			--game->empty;
		}
		ptr += 2;
		if (*ptr == '\n') {
			if (end != '\n')
				fatal(E_LOAD);
			ptr += game->cols + 2;
		} else if (end != ',') {
			fatal(E_LOAD);
		}
	}

	if (fgetc(f) != EOF)
		fatal(E_LOAD);

	fclose(f);
}

/*
 * Saves a game instance to the file at 'path' if possible. Human readable
 * messages are printed to stderr to indicate success or failure.
 */
static void savegame(struct game *game, const char *path)
{
	FILE *f;
	char *ptr = game->horz.start;

	f = fopen(path, "w");
	if (!f) {
		fputs("Can not open file for write\n", stderr);
		return;
	}

	fprintf(f, "%d\n", game->next + 1);

	while (*ptr) {
		if (*ptr == '|' || *ptr == '-') {
			fputc('1', f);
		} else if (*ptr == ' ') {
			fputc('0', f);
		} else {
			fputc('\n', f);
			++ptr;
			continue;
		}
		ptr += 2;
	}

	ptr = game->vert.start + 1;
	while (*ptr) {
		fprintf(f, "%d", *ptr == ' ' ? 0 : ID(*ptr) + 1);
		ptr += 2;
		do {
			fprintf(f, ",%d", *ptr == ' ' ? 0 : ID(*ptr) + 1);
			ptr += 2;
		} while (*ptr != '\n');
		fputc('\n', f);
		ptr += game->cols + 2;
	}

	fclose(f);
	fputs("Save complete\n", stderr);
}

/*
 * Reads and parses a full line from stdin. If a valid move is read, 'args' is
 * initialised appropriately.
 *
 * RETURNS:
 * The type of move.
 *
 * CALLS FATAL() WITH:
 * - E_EOF if reading the first character of the line fails due to EOF.
 */
static enum move getmove(struct game *game, union args *args)
{
	struct grid *tmp = &game->horz;
	size_t i;
	int c;

	args->place.h = fgeti(stdin, &c, DIMEN_MAX);
	if (args->place.h < 0) {
		if (c == EOF) {
			fatal(E_EOF);
		} else if (c == 'w' && (c = getchar()) == ' ') {
			for (i = 0; i < sizeof args->path - 1; ++i) {
				c = getchar();
				if (ISEOL(c)) {
					args->path[i] = 0;
					return i > 0 ? M_SAVE : M_NONE;
				}
				args->path[i] = c;
			}
		}
	} else if (c == ' ') {
		args->place.w = fgeti(stdin, &c, DIMEN_MAX);
		if (args->place.w >= 0 && c == ' ') {
			c = getchar();
			switch (c) {
			case 'v':
				tmp = &game->vert;
			case 'h':
				c = getchar();
				if (ISEOL(c)) {
					args->place.grid = tmp;
					args->place.cols = game->cols;
					args->place.name = NAME(game->next);
					return M_PLACE;
				}
			}
		}
	}

	while (!ISEOL(c))
		c = getchar();
	return M_NONE;
}

/*
 * Converts the cell pointed to by 'loc' into a box owned by the player named
 * 'name' if it is not already owned and is surrounded by completed edges.
 *
 * RETURN VALUE:
 * 1 when a box is formed, otherwise 0.
 */
static int makebox(char *loc, int cols, int name)
{
	int sum;

	if (*loc == ' ') {
		sum = loc[-cols] + loc[-1] + loc[1] + loc[cols];
		if (sum == 2 * ('|' + '-')) {
			*loc = name;
			return 1;
		}
	}

	return 0;
}

/*
 * Executes a M_PLACE move.
 *
 * RETURN VALUE:
 * -1 is returned if the move is out of bounds, otherwise the number of boxes
 * formed by placing the edge is returned.
 */
static int doplace(struct place *args)
{
	struct grid *grid = args->grid;
	char *loc;
	int score = 0;

	if (args->h > grid->height || args->w > grid->width)
		return -1;
	loc = grid->start + 2 * args->cols * args->h + 2 * args->w;
	if (*loc != ' ')
		return -1;

	*loc = grid->rep;
	score += makebox(loc - grid->offset, args->cols, args->name);
	score += makebox(loc + grid->offset, args->cols, args->name);
	return score;
}

/*
 * The main game loop.
 *
 * CALLS FATAL() WITH:
 * - E_EOF if EOF is reached on stdin before the game is over.
 */
static void play(struct game *game)
{
	union args args;
	int score;

	while (game->empty) {
		PGRID(game);
		score = -1;
		do {
			printf("%c> ", NAME(game->next));
			fflush(stdout);
			switch (getmove(game, &args)) {
			case M_SAVE:
				savegame(game, args.path);
			case M_NONE:
				continue;
			case M_PLACE:
				score = doplace(&args.place);
			}
		} while (score < 0);

		if (score) {
			game->scores[game->next] += score;
			game->empty -= score;
		} else {
			game->next = (game->next + 1) % game->players;
		}
	}
}

/*
 * Prints the game winners to stdout in a human readable format.
 */
static void pwinners(struct game *game)
{
	long int max = game->scores[0];
	char winners[PLAYER_MAX + 1];
	char *ptr = winners;
	int i;

	*ptr++ = NAME(0);
	for (i = 1; i < game->players; ++i) {
		if (game->scores[i] >= max) {
			if (game->scores[i] > max) {
				ptr = winners;
				max = game->scores[i];
			}
			*ptr++ = NAME(i);
		}
	}
	*ptr = 0;

	ptr = winners;
	printf("Winner(s): %c", *ptr++);
	while (*ptr)
		printf(", %c", *ptr++);
	putchar('\n');
}

/* External Function Definitions -------------------------------------------*/

int main(int argc, char **argv)
{
	struct game game;
	char *end;
	long int height, width, players;

	if (argc < ARGC_MIN || argc > ARGC_MAX)
		fatal(E_USAGE);

	height = strtol(argv[ARGV_HEIGHT], &end, 10);
	if (height < DIMEN_MIN || height > DIMEN_MAX || *end)
		fatal(E_DIMEN);
	width = strtol(argv[ARGV_WIDTH], &end, 10);
	if (width < DIMEN_MIN || width > DIMEN_MAX || *end)
		fatal(E_DIMEN);

	players = strtol(argv[ARGV_PLAYERS], &end, 10);
	if (players < PLAYER_MIN || players > PLAYER_MAX || *end)
		fatal(E_PLAYERS);

	newgame(&game, height, width, players);
	if (argc == ARGC_MAX)
		loadgame(&game, argv[ARGV_PATH]);

	play(&game);
	PGRID(&game);
	pwinners(&game);
	return 0;
}

/* End of File -------------------------------------------------------------*/
