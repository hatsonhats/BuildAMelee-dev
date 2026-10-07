/* The aerial catalog: every aerial a build can borrow (id, character, slot).
 *
 * Called from: never; read through catalog_aerials.c.
 * State: none (constant table).
 */
#include <engine/catalog.h>
const BamAerialDef bam_aerials[BAM_AERIALS] = {
    { 1, 0, 2, 0 }, /* Captain Falcon Neutral Air */
    { 2, 0, 2, 1 }, /* Captain Falcon Forward Air */
    { 3, 0, 2, 2 }, /* Captain Falcon Back Air */
    { 4, 0, 2, 3 }, /* Captain Falcon Up Air */
    { 5, 0, 2, 4 }, /* Captain Falcon Down Air */
    { 6, 1, 3, 0 }, /* Donkey Kong Neutral Air */
    { 7, 1, 3, 1 }, /* Donkey Kong Forward Air */
    { 8, 1, 3, 2 }, /* Donkey Kong Back Air */
    { 9, 1, 3, 3 }, /* Donkey Kong Up Air */
    { 10, 1, 3, 4 }, /* Donkey Kong Down Air */
    { 11, 2, 1, 0 }, /* Fox Neutral Air */
    { 12, 2, 1, 1 }, /* Fox Forward Air */
    { 13, 2, 1, 2 }, /* Fox Back Air */
    { 14, 2, 1, 3 }, /* Fox Up Air */
    { 15, 2, 1, 4 }, /* Fox Down Air */
    { 16, 3, 24, 0 }, /* Mr Game and Watch Neutral Air */
    { 17, 3, 24, 1 }, /* Mr Game and Watch Forward Air */
    { 18, 3, 24, 2 }, /* Mr Game and Watch Back Air */
    { 19, 3, 24, 3 }, /* Mr Game and Watch Up Air */
    { 20, 3, 24, 4 }, /* Mr Game and Watch Down Air */
    { 21, 4, 4, 0 }, /* Kirby Neutral Air */
    { 22, 4, 4, 1 }, /* Kirby Forward Air */
    { 23, 4, 4, 2 }, /* Kirby Back Air */
    { 24, 4, 4, 3 }, /* Kirby Up Air */
    { 25, 4, 4, 4 }, /* Kirby Down Air */
    { 26, 5, 5, 0 }, /* Bowser Neutral Air */
    { 27, 5, 5, 1 }, /* Bowser Forward Air */
    { 28, 5, 5, 2 }, /* Bowser Back Air */
    { 29, 5, 5, 3 }, /* Bowser Up Air */
    { 30, 5, 5, 4 }, /* Bowser Down Air */
    { 31, 6, 6, 0 }, /* Link Neutral Air */
    { 32, 6, 6, 1 }, /* Link Forward Air */
    { 33, 6, 6, 2 }, /* Link Back Air */
    { 34, 6, 6, 3 }, /* Link Up Air */
    { 35, 6, 6, 4 }, /* Link Down Air */
    { 36, 7, 17, 0 }, /* Luigi Neutral Air */
    { 37, 7, 17, 1 }, /* Luigi Forward Air */
    { 38, 7, 17, 2 }, /* Luigi Back Air */
    { 39, 7, 17, 3 }, /* Luigi Up Air */
    { 40, 7, 17, 4 }, /* Luigi Down Air */
    { 41, 8, 0, 0 }, /* Mario Neutral Air */
    { 42, 8, 0, 1 }, /* Mario Forward Air */
    { 43, 8, 0, 2 }, /* Mario Back Air */
    { 44, 8, 0, 3 }, /* Mario Up Air */
    { 45, 8, 0, 4 }, /* Mario Down Air */
    { 46, 9, 18, 0 }, /* Marth Neutral Air */
    { 47, 9, 18, 1 }, /* Marth Forward Air */
    { 48, 9, 18, 2 }, /* Marth Back Air */
    { 49, 9, 18, 3 }, /* Marth Up Air */
    { 50, 9, 18, 4 }, /* Marth Down Air */
    { 51, 10, 16, 0 }, /* Mewtwo Neutral Air */
    { 52, 10, 16, 1 }, /* Mewtwo Forward Air */
    { 53, 10, 16, 2 }, /* Mewtwo Back Air */
    { 54, 10, 16, 3 }, /* Mewtwo Up Air */
    { 55, 10, 16, 4 }, /* Mewtwo Down Air */
    { 56, 11, 8, 0 }, /* Ness Neutral Air */
    { 57, 11, 8, 1 }, /* Ness Forward Air */
    { 58, 11, 8, 2 }, /* Ness Back Air */
    { 59, 11, 8, 3 }, /* Ness Up Air */
    { 60, 11, 8, 4 }, /* Ness Down Air */
    { 61, 12, 9, 0 }, /* Peach Neutral Air */
    { 62, 12, 9, 1 }, /* Peach Forward Air */
    { 63, 12, 9, 2 }, /* Peach Back Air */
    { 64, 12, 9, 3 }, /* Peach Up Air */
    { 65, 12, 9, 4 }, /* Peach Down Air */
    { 66, 13, 12, 0 }, /* Pikachu Neutral Air */
    { 67, 13, 12, 1 }, /* Pikachu Forward Air */
    { 68, 13, 12, 2 }, /* Pikachu Back Air */
    { 69, 13, 12, 3 }, /* Pikachu Up Air */
    { 70, 13, 12, 4 }, /* Pikachu Down Air */
    { 71, 14, 10, 0 }, /* Ice Climbers Neutral Air */
    { 72, 14, 10, 1 }, /* Ice Climbers Forward Air */
    { 73, 14, 10, 2 }, /* Ice Climbers Back Air */
    { 74, 14, 10, 3 }, /* Ice Climbers Up Air */
    { 75, 14, 10, 4 }, /* Ice Climbers Down Air */
    { 76, 15, 15, 0 }, /* Jigglypuff Neutral Air */
    { 77, 15, 15, 1 }, /* Jigglypuff Forward Air */
    { 78, 15, 15, 2 }, /* Jigglypuff Back Air */
    { 79, 15, 15, 3 }, /* Jigglypuff Up Air */
    { 80, 15, 15, 4 }, /* Jigglypuff Down Air */
    { 81, 16, 13, 0 }, /* Samus Neutral Air */
    { 82, 16, 13, 1 }, /* Samus Forward Air */
    { 83, 16, 13, 2 }, /* Samus Back Air */
    { 84, 16, 13, 3 }, /* Samus Up Air */
    { 85, 16, 13, 4 }, /* Samus Down Air */
    { 86, 17, 14, 0 }, /* Yoshi Neutral Air */
    { 87, 17, 14, 1 }, /* Yoshi Forward Air */
    { 88, 17, 14, 2 }, /* Yoshi Back Air */
    { 89, 17, 14, 3 }, /* Yoshi Up Air */
    { 90, 17, 14, 4 }, /* Yoshi Down Air */
    { 91, 18, 19, 0 }, /* Zelda Neutral Air */
    { 92, 18, 19, 1 }, /* Zelda Forward Air */
    { 93, 18, 19, 2 }, /* Zelda Back Air */
    { 94, 18, 19, 3 }, /* Zelda Up Air */
    { 95, 18, 19, 4 }, /* Zelda Down Air */
    { 96, 19, 7, 0 }, /* Sheik Neutral Air */
    { 97, 19, 7, 1 }, /* Sheik Forward Air */
    { 98, 19, 7, 2 }, /* Sheik Back Air */
    { 99, 19, 7, 3 }, /* Sheik Up Air */
    { 100, 19, 7, 4 }, /* Sheik Down Air */
    { 101, 20, 22, 0 }, /* Falco Neutral Air */
    { 102, 20, 22, 1 }, /* Falco Forward Air */
    { 103, 20, 22, 2 }, /* Falco Back Air */
    { 104, 20, 22, 3 }, /* Falco Up Air */
    { 105, 20, 22, 4 }, /* Falco Down Air */
    { 106, 21, 20, 0 }, /* Young Link Neutral Air */
    { 107, 21, 20, 1 }, /* Young Link Forward Air */
    { 108, 21, 20, 2 }, /* Young Link Back Air */
    { 109, 21, 20, 3 }, /* Young Link Up Air */
    { 110, 21, 20, 4 }, /* Young Link Down Air */
    { 111, 22, 21, 0 }, /* Dr Mario Neutral Air */
    { 112, 22, 21, 1 }, /* Dr Mario Forward Air */
    { 113, 22, 21, 2 }, /* Dr Mario Back Air */
    { 114, 22, 21, 3 }, /* Dr Mario Up Air */
    { 115, 22, 21, 4 }, /* Dr Mario Down Air */
    { 116, 23, 26, 0 }, /* Roy Neutral Air */
    { 117, 23, 26, 1 }, /* Roy Forward Air */
    { 118, 23, 26, 2 }, /* Roy Back Air */
    { 119, 23, 26, 3 }, /* Roy Up Air */
    { 120, 23, 26, 4 }, /* Roy Down Air */
    { 121, 24, 23, 0 }, /* Pichu Neutral Air */
    { 122, 24, 23, 1 }, /* Pichu Forward Air */
    { 123, 24, 23, 2 }, /* Pichu Back Air */
    { 124, 24, 23, 3 }, /* Pichu Up Air */
    { 125, 24, 23, 4 }, /* Pichu Down Air */
    { 126, 25, 25, 0 }, /* Ganondorf Neutral Air */
    { 127, 25, 25, 1 }, /* Ganondorf Forward Air */
    { 128, 25, 25, 2 }, /* Ganondorf Back Air */
    { 129, 25, 25, 3 }, /* Ganondorf Up Air */
    { 130, 25, 25, 4 }, /* Ganondorf Down Air */
};
