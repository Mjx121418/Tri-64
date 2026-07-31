#ifndef TEST_LEVEL_SCRIPT_H
#define TEST_LEVEL_SCRIPT_H

// Runs the level script of course "Bob-omb Battlefield" (LEVEL_BOB = 9) from
// the vanilla baserom and prints the resulting area graph node trees.
//
// The entry point is the game's own level jump table (script_exec_level_table,
// segment 0x15): it reads the current level number into the register, then
// JUMP_IF dispatches to the matching course script, which EXECUTEs the course
// entry. Everything downstream (segment ROM ranges, geo layouts, display
// lists) is read from the ROM itself, exactly like the game does.
void testRunLevelScript();

#endif /* TEST_LEVEL_SCRIPT_H */
