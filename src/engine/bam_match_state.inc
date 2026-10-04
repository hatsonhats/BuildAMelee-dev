/* Per-match state of the engine files, kept in BamMatchState (match heap). */
    /* anim_scale.c */
    Scaled scaled[BAM_FIGHTERS * 6];
    unsigned scaled_count;
    RotFix rotfix[BAM_FIGHTERS * ROTFIX_PER_FIGHTER];
    unsigned char rotfix_part[BAM_FIGHTERS * ROTFIX_PER_FIGHTER];
    unsigned short rotfix_hash[ROTFIX_HASH];
    unsigned char joint_parent[BAM_FIGHTERS][POSE_JOINTS];
    unsigned char pose_on[BAM_FIGHTERS];
    PropTrack prop_tracks[BAM_FIGHTERS][PROP_TRACKS];
    unsigned char prop_track_count[BAM_FIGHTERS];
    PropHit prop_hits[BAM_FIGHTERS * 4];
    short last_prop[BAM_FIGHTERS], last_root[BAM_FIGHTERS];
    PropAnchor anchors[BAM_FIGHTERS][ANCHORS];
    unsigned char anchor_next[BAM_FIGHTERS];
    /* sword_visual.c */
    HSD_JObj* weapons[BAM_FIGHTERS][WEAPON_ITEMS];
    Fighter* weapon_owner[BAM_FIGHTERS];
    DonorModel* donor_models[BAM_FIGHTERS];
    signed char donor_vis[BAM_FIGHTERS][VIS_GROUPS];
    unsigned char donor_vis_kind[BAM_FIGHTERS];
    unsigned char parasol_float[BAM_FIGHTERS];
    Mtx weapon_attach[BAM_FIGHTERS][WEAPON_ITEMS];
    Mtx parasol_rel[BAM_FIGHTERS];
    /* rest_sleep.c */
    Fighter* sleepers[BAM_FIGHTERS];
