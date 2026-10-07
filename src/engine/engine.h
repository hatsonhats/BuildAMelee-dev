/* Everything the borrowed-move engine offers the rest of the mod and the
 * edited retail code (overrides/): include this one header.
 *
 *   fighter.h      loadouts, build fighters, the match block
 *   catalog.h      the moves players can pick
 *   borrow.h       borrowing a move: specials, aerials, normals
 *   donor_load.h   loading a donor's files
 *   anim/anim.h    posing borrowed animations; hitboxes, rebuilt bones, articles
 *   visual/visual.h  drawing the donor's parts */
#ifndef BAM_ENGINE_H
#define BAM_ENGINE_H
#include <engine/fighter.h>
#include <engine/catalog.h>
#include <engine/borrow.h>
#include <engine/donor_load.h>
#include <engine/anim/anim.h>
#include <engine/visual/visual.h>
/* Provided by platform/training.c: hitbox and hurtbox display filters
 * (1 outside training). */
int Bam_DrawHitboxes(void);
int Bam_DrawHurtboxes(void);
/* Full-charge flash of a borrowed charge move (borrow.c; ftcolanim.c fix). */
void Bam_ChargeFlash(Fighter* fp);
#endif
