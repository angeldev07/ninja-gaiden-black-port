/*
 * remote.h: a running session driven from outside (--remote), for measurements in a game the
 * owner already has in front of enemies: a local named pipe that takes one command per line.
 *
 *     input <script>   presses for the game now, --input-script syntax, times counted from now
 *     trace on|off     --ach-trace (moves, defeats, health; the per-frame player object)
 *     god on|off       keeps player 1's health at its max (ach.c; the game files are not touched)
 *     shot             a capture of the next frame, as F12
 *     status           mode, chapter, play time, health, ki, move, weapon, defeats, polls
 *
 * Client: python -m ngb_tools.remote "status" "input t=0p:Y" ...
 */
#ifndef NGB_REMOTE_H
#define NGB_REMOTE_H

#define NGB_REMOTE_PIPE "\\\\.\\pipe\\ngb-loader"

void ngb_remote_start(void);

#endif
