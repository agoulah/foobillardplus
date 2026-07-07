/* aiplayer.c
**
**    code for positioning artifitial intelligence player
**    Copyright (C) 2001  Florian Berger
**    Email: harpin_floh@yahoo.de, florian.berger@jk.uni-linz.ac.at
**
**    Updated Version foobillard++ started at 12/2010
**    Copyright (C) 2010 - 2013 Holger Schaekel (foobillardplus@go4more.de)
**
**    This program is free software; you can redistribute it and/or modify
**    it under the terms of the GNU General Public License Version 2 as
**    published by the Free Software Foundation;
**
**    This program is distributed in the hope that it will be useful,
**    but WITHOUT ANY WARRANTY; without even the implied warranty of
**    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**    GNU General Public License for more details.
**
**    You should have received a copy of the GNU General Public License
**    along with this program; if not, write to the Free Software
**    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
*/

#define AIPLAYER_C
#include "aiplayer.h"
#include "vmath.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

VMvect (*ai_get_stroke_dir)( BallsType * balls, BordersType * walls, struct Player *pplayer ) = ai_get_stroke_dir_8ball;

static VMfloat ai_skill=1.0; /* not used ...yet */
static VMfloat ai_err=0.0;

/***********************************************************************/

void ai_set_skill( VMfloat skill ) /* not used ...yet */
{
    ai_skill=skill;
}

/***********************************************************************/

void ai_set_err( VMfloat err )
{
    ai_err=err;
}

/***********************************************************************/

static int my_rand(int nr)
{
    return rand()%nr;
}

/***********************************************************************/

static VMfloat my_rand01(void)
{
    return (VMfloat)rand()/(VMfloat)RAND_MAX;
}

/***********************************************************************/

VMfloat stroke_angle( BallType * bcue, BallType * bhit, HoleType * hole )
{
    VMvect r_hit;

    r_hit = vec_scale(vec_unit(vec_diff(bhit->r,hole->aim)),(bcue->d+bhit->d)/2.0);
    r_hit = vec_add(bhit->r,r_hit);
    return(
           vec_angle(vec_diff(r_hit,bcue->r), vec_diff(hole->aim,r_hit))
          );
}

/***********************************************************************/

static int ball_in_way_ign( int ballnr, VMvect aim, BallsType * balls, int ignore )
{
    VMvect way, iball;
    VMfloat par, norm, lway;
    int inway=0;
    int i;

    for(i=0;i<balls->nr;i++) {
      if( balls->ball[i].in_game && i!=ballnr && i!=ignore ){
        way   = vec_diff(aim,balls->ball[ballnr].r);
        lway  = vec_abs(way);
        iball = vec_diff(balls->ball[i].r,balls->ball[ballnr].r);
        par   = vec_mul(vec_unit(way),iball);
        norm  = vec_abs(vec_cross(vec_unit(way),iball));
        if( par>0.0 && par<lway && norm<(balls->ball[i].d+balls->ball[ballnr].d)/2.0 ){
            //fprintf(stderr,"ball_in_way:%d (ballnr=%d)\n",i,ballnr);
            inway=1;
            break;
        }
      }
    }
    return( inway );
}

/***********************************************************************/

int ball_in_way( int ballnr, VMvect aim, BallsType * balls )
{
    return ball_in_way_ign( ballnr, aim, balls, -1 );
}

/***********************************************************************/

int ind_ball_nr( int nr, BallsType * balls )
{
    int i;
    for( i=0 ; i<balls->nr ; i++ ){
        if( balls->ball[i].nr == nr ) break;
    }
    return i;
}

/***********************************************************************/

int nth_in_game( int n, BallsType * balls, int full_half )
{
    int i;
    for( i=0; i<balls->nr && n>=0; i++ ){
        if( full_half == BALL_FULL && balls->ball[i].nr<8 && balls->ball[i].nr>0 ){
            n--;
        }
        if( full_half == BALL_HALF && balls->ball[i].nr>8 ){
            n--;
        }
        if( full_half == BALL_ANY && ( balls->ball[i].nr>8 || (balls->ball[i].nr<8 && balls->ball[i].nr>0) )){
            n--;
        }
    }
    return i;
}

/***********************************************************************
 *        Simulation based shot selection for 8ball and 9ball          *
 *                                                                     *
 * The old AI aimed only at the geometrically easiest pot and - if no  *
 * clean pot existed - banged the cue ball at a random legal ball.     *
 * That produced fouls (blocked cue paths, scratches) and never any    *
 * defensive play.  The new selection generates candidate shots        *
 * (pots, safeties and one-cushion kick shots when snookered), plays   *
 * each of them on a copy of the table with the real physics engine    *
 * and rates the outcome with the same rules evaluate_last_move()      *
 * applies afterwards.  The best rated candidate is played.  When a    *
 * pot is found, draw/follow/side-english variants are simulated too   *
 * and kept when they leave a better position (the chosen cue offset   *
 * is written to player->cue_x/cue_y alias queue_point_x/_y).  The     *
 * player err is applied to direction, strength and english after the  *
 * choice, so weaker AI players still miss.                            *
 ***********************************************************************/

#define AI_CUE_MAXSPEED   7.0     /* keep in sync with CUEBALL_MAXSPEED (billard3d.h) */
#define AI_CUE_ELEV_XY    0.9986  /* sin(87deg), default cue elevation: only used for
                                     the strength estimate - the simulation itself
                                     computes the real elevation via cue_min_Xque() */
#define AI_SIM_DT         0.01    /* same timestep as the real game loop */
#define AI_SIM_MAXSTEPS   1200    /* max 12s of simulated game time per shot */
#define AI_MAX_CAND       160
#define AI_POT_SIMS       14      /* pot candidates that get simulated */
#define AI_SAFETY_SIMS    24      /* safety candidates that get simulated */
#define AI_KICK_SIMS      56      /* kick candidates that get simulated (rare, and
                                     connecting off a cushion needs a fine grid) */
#define AI_MAX_CUT_ANGLE  1.4     /* rad (~80deg): steeper cuts don't count as pots */
#define AI_SCORE_WIN      10000.0
#define AI_SCORE_GOOD_POT 100.0   /* below this no reliable pot was found */

#define AI_MAX_ENGLISH ((BALL_D-QUEUE_D2)/2.0)  /* same cue offset limit as setenglish() */

typedef struct {
    VMvect  target;    /* absolute point to drive the cue ball at */
    VMfloat strength;  /* cue strength 0..1 */
    VMfloat side;      /* cue tip offset: side english (queue_point_x) */
    VMfloat vert;      /* cue tip offset: draw(>0)/follow(<0) (queue_point_y) */
    VMfloat prior;     /* geometric pre-rating, only used for pruning */
} AIShot;

static BallsType sim_balls = {0,GAME_8BALL,NULL};  /* reusable simulation table */

/***********************************************************************/

static void ai_sim_load( BallsType * src )
{
    int i;

    if( sim_balls.ball==NULL || sim_balls.nr!=src->nr ){
        free( sim_balls.ball );
        sim_balls.ball=(BallType *)malloc(src->nr*sizeof(BallType));
        sim_balls.nr=src->nr;
    }
    sim_balls.gametype=src->gametype;
    for( i=0; i<src->nr; i++ ){
        sim_balls.ball[i]=src->ball[i];
        /* the simulated balls must never touch the real path buffers */
        sim_balls.ball[i].path=NULL;
        sim_balls.ball[i].pathsize=0;
        sim_balls.ball[i].pathcnt=0;
    }
}

/***********************************************************************/

static void ai_sim_shot( BallsType * balls, BordersType * walls, VMvect target,
                         VMfloat strength, VMfloat side, VMfloat vert )
{
    VMvect dir, dirq, nx, ny, hitpoint;
    VMfloat d, xque, sinx, cosx;
    int i;

    ai_sim_load( balls );
    dir=vec_diff(target,balls->ball[0].r);
    dir.z=0.0;
    if( vec_abs(dir)<1.0E-6 ) dir=vec_xyz(0.0,1.0,0.0);
    dir=vec_unit(dir);
    /* the same stroke queue_shot() performs: the cue elevation is the
       lowest one at which the stick clears balls and cushions (like
       check_cue() enforces for the real shot), dirq is the cue
       direction, v=-dirq*speed, spin from the tip offset */
    xque=cue_min_Xque(balls,0,dir,side,vert);
    sinx=sin(xque*M_PI/180.0);
    cosx=cos(xque*M_PI/180.0);
    dirq=vec_add(vec_scale(dir,sinx),vec_scale(vec_ez(),cosx));
    sim_balls.ball[0].v=vec_scale(dirq,-AI_CUE_MAXSPEED*strength);
    if( !options_jump_shots ) sim_balls.ball[0].v.z=0.0;
    if( side==0.0 && vert==0.0 ){
        sim_balls.ball[0].w=vec_xyz(0.0,0.0,0.0);
    } else {
        nx=vec_unit(vec_cross(vec_ez(),dirq));
        ny=vec_unit(vec_cross(nx,dirq));
        hitpoint=vec_add(vec_scale(nx,side),vec_scale(ny,vert));
        d=sim_balls.ball[0].d;
        sim_balls.ball[0].w=vec_scale(vec_cross(dirq,hitpoint),2.0*3.0*AI_CUE_MAXSPEED*strength/d/d);
    }
    for( i=0; i<AI_SIM_MAXSTEPS; i++ ){
        if( !proceed_dt(&sim_balls,walls,AI_SIM_DT,NULL) ) break;
    }
}

/***********************************************************************/

static VMfloat ai_pot_quality( BallsType * balls, BordersType * walls, int i )
/* how promising is the best direct pot of ball i (0=no pot possible) */
{
    VMvect r_hit;
    VMfloat ang, cosang, dc, dh, q, best=0.0;
    BallType *bcue=&balls->ball[0], *bhit=&balls->ball[i];
    HoleType *hole;
    int j;

    if( !bcue->in_game || !bhit->in_game ) return 0.0;
    for( j=0; j<walls->holenr; j++ ){
        hole=&walls->hole[j];
        r_hit=vec_add(bhit->r,vec_scale(vec_unit(vec_diff(bhit->r,hole->aim)),(bcue->d+bhit->d)/2.0));
        if( ball_in_way_ign(0,r_hit,balls,i) || ball_in_way(i,hole->aim,balls) ) continue;
        ang=fabs(vec_angle(vec_diff(r_hit,bcue->r),vec_diff(hole->aim,r_hit)));
        if( ang>=AI_MAX_CUT_ANGLE ) continue;
        cosang=cos(ang);
        dc=vec_abs(vec_diff(r_hit,bcue->r));
        dh=vec_abs(vec_diff(hole->aim,bhit->r));
        q=cosang*cosang/(1.0+1.2*(dc+dh));
        if( q>best ) best=q;
    }
    return best;
}

/***********************************************************************/

static int ai_8ball_pottable( BallsType * balls, int i, int half_full )
/* may ball i be legally pocketed by the player on half_full */
{
    int nr=balls->ball[i].nr;

    if( !balls->ball[i].in_game || nr==0 ) return 0;
    if( nr==8 ) return( balls_in_game(balls,half_full)==0 );
    switch( half_full ){
      case BALL_FULL: return( nr<8 );
      case BALL_HALF: return( nr>8 );
      default:        return 1;   /* open table */
    }
}

/***********************************************************************/

static VMfloat ai_best_pot_quality_8ball( BallsType * balls, BordersType * walls, int half_full )
{
    VMfloat q, best=0.0;
    int i;

    for( i=1; i<balls->nr; i++ ){
        if( ai_8ball_pottable(balls,i,half_full) ){
            q=ai_pot_quality(balls,walls,i);
            if( q>best ) best=q;
        }
    }
    return best;
}

/***********************************************************************/

static int ai_lowest_ball( BallsType * balls )
/* index of the lowest numbered ball on the table (9ball), -1 if none */
{
    int i, lowest=-1;

    for( i=1; i<balls->nr; i++ ){
        if( balls->ball[i].in_game && balls->ball[i].nr!=0 &&
            (lowest==-1 || balls->ball[i].nr<balls->ball[lowest].nr) ) lowest=i;
    }
    return lowest;
}

/***********************************************************************/

static int ai_in_strafraum( VMvect pos )
/* same penalty area check evaluate_last_move_8ball uses */
{
    return( pos.y < -TABLE_L/4.0 );
}

/***********************************************************************/

static VMfloat ai_score_8ball( BordersType * walls, struct Player * pplayer )
/* rate the outcome in sim_balls/move log with the evaluate_last_move_8ball rules */
{
    int hf=pplayer->half_full;
    int first=BM_get_1st_ball_hit();
    int out_full=BM_get_balls_out_full();
    int out_half=BM_get_balls_out_half();
    int foul=0, own_out, opp_out, opp_hf, own_left=0;
    int i;
    VMfloat score=0.0;

    switch( hf ){
      case BALL_FULL:
        own_out=out_full; opp_out=out_half; opp_hf=BALL_HALF;
        if( first>8 && first<16 ) foul=1;
        break;
      case BALL_HALF:
        own_out=out_half; opp_out=out_full; opp_hf=BALL_FULL;
        if( first>0 && first<8 ) foul=1;
        break;
      default:
        own_out=out_full+out_half; opp_out=0; opp_hf=BALL_ANY;
        break;
    }
    for( i=1; i<sim_balls.nr; i++ ){
        if( sim_balls.ball[i].nr!=8 && ai_8ball_pottable(&sim_balls,i,hf) ) own_left++;
    }
    if( first==8 && (hf==BALL_FULL || hf==BALL_HALF) && own_left>0 ) foul=1;
    if( BM_get_balls_hit()==0 ) foul=1;
    if( BM_get_white_out() ) foul=1;
    if( pplayer->place_cue_ball && ai_in_strafraum(BM_get_1st_ball_hit_pos()) &&
        !BM_get_non_strafraum_wall_hit_before_1st_ball(ai_in_strafraum) ) foul=1;

    if( BM_get_ball_out(8) ){
        /* the 8 alone as the very last own ball wins, everything else loses */
        if( !foul && hf!=BALL_ANY && own_out==0 && own_left==0 ) return AI_SCORE_WIN;
        return -AI_SCORE_WIN;
    }

    score += 120.0*(VMfloat)own_out;
    score -= 60.0*(VMfloat)opp_out;
    if( foul ){
        score -= 650.0;   /* opponent gets the cue ball in hand */
    } else if( own_out>0 ){
        /* we stay at the table: reward a good position for the next pot */
        score += 250.0*ai_best_pot_quality_8ball(&sim_balls,walls,hf);
    } else {
        /* turn passes: the harder the opponents best pot, the better the safety */
        score -= 250.0*ai_best_pot_quality_8ball(&sim_balls,walls,opp_hf);
    }
    return score;
}

/***********************************************************************/

static VMfloat ai_score_9ball( BordersType * walls, int minball_before )
/* rate the outcome in sim_balls/move log with the evaluate_last_move_9ball rules */
{
    int first=BM_get_1st_ball_hit();
    int foul=0, potted=0, lowest;
    int i;
    VMfloat score=0.0;

    if( BM_get_balls_hit()==0 ) foul=1;
    if( first!=minball_before ) foul=1;
    if( BM_get_white_out() ) foul=1;

    if( BM_get_ball_out(9) ) return( foul ? -AI_SCORE_WIN : AI_SCORE_WIN );

    for( i=1; i<=15; i++ ) if( BM_get_ball_out(i) ) potted++;
    lowest=ai_lowest_ball(&sim_balls);

    score += 90.0*(VMfloat)potted;
    if( foul ){
        score -= 650.0;   /* opponent gets the cue ball in hand */
    } else if( potted>0 ){
        if( lowest!=-1 ) score += 250.0*ai_pot_quality(&sim_balls,walls,lowest);
    } else {
        if( lowest!=-1 ) score -= 250.0*ai_pot_quality(&sim_balls,walls,lowest);
    }
    return score;
}

/***********************************************************************/

static int ai_add_shot( AIShot * list, int n, VMvect target, VMfloat strength, VMfloat prior )
{
    if( n<AI_MAX_CAND ){
        list[n].target=target;
        list[n].strength=strength;
        list[n].side=0.0;
        list[n].vert=0.0;
        list[n].prior=prior;
        n++;
    }
    return n;
}

/***********************************************************************/

static int ai_shot_cmp( const void * a, const void * b )
{
    VMfloat pa=((const AIShot *)a)->prior;
    VMfloat pb=((const AIShot *)b)->prior;
    return( (pa<pb) ? 1 : ((pa>pb) ? -1 : 0) );
}

/***********************************************************************/

static VMfloat ai_pot_strength( VMfloat d_cue, VMfloat d_hole, VMfloat cosang )
/* estimate the cue strength needed to roll the object ball into the hole */
{
    VMfloat v_obj, v_imp, v0;

    if( cosang<0.25 ) cosang=0.25;
    v_obj=sqrt(2.0*0.35*d_hole)+0.4;          /* speed the object ball needs */
    v_imp=v_obj/cosang;                       /* cue ball speed at impact */
    v0=sqrt(v_imp*v_imp+2.0*0.7*d_cue);       /* sliding/rolling losses on the way */
    v0=v0/(AI_CUE_MAXSPEED*AI_CUE_ELEV_XY);
    if( v0<0.15 ) v0=0.15;
    if( v0>1.0 )  v0=1.0;
    return v0;
}

/***********************************************************************/

static int ai_gen_pot_shots( BallsType * balls, BordersType * walls, AIShot * list, int n, int i )
/* pot candidates for object ball i: every hole with free paths */
{
    VMvect r_hit;
    VMfloat ang, cosang, dc, dh, prior, s;
    BallType *bcue=&balls->ball[0], *bhit=&balls->ball[i];
    HoleType *hole;
    int j;

    for( j=0; j<walls->holenr; j++ ){
        hole=&walls->hole[j];
        r_hit=vec_add(bhit->r,vec_scale(vec_unit(vec_diff(bhit->r,hole->aim)),(bcue->d+bhit->d)/2.0));
        if( ball_in_way_ign(0,r_hit,balls,i) || ball_in_way(i,hole->aim,balls) ) continue;
        ang=fabs(vec_angle(vec_diff(r_hit,bcue->r),vec_diff(hole->aim,r_hit)));
        if( ang>=AI_MAX_CUT_ANGLE ) continue;
        cosang=cos(ang);
        dc=vec_abs(vec_diff(r_hit,bcue->r));
        dh=vec_abs(vec_diff(hole->aim,bhit->r));
        prior=cosang*cosang/(1.0+1.2*(dc+dh));
        s=ai_pot_strength(dc,dh,cosang);
        n=ai_add_shot(list,n,r_hit,s,prior);
        n=ai_add_shot(list,n,r_hit,(s*1.4>1.0)?1.0:s*1.4,prior*0.85);
    }
    return n;
}

/***********************************************************************/

static int ai_gen_safety_shots( BallsType * balls, AIShot * list, int n, int i )
/* soft legal hits on ball i: full and thin contacts at low strength */
{
    static const double phi[5]={0.0,0.7,-0.7,1.1,-1.1};
    VMvect along, perp, ghost;
    VMfloat rr;
    BallType *bcue=&balls->ball[0], *bhit=&balls->ball[i];
    int k;

    along=vec_diff(bhit->r,bcue->r);
    along.z=0.0;
    if( vec_abs(along)<1.0E-4 ) return n;
    along=vec_unit(along);
    perp=vec_unit(vec_cross(vec_ez(),along));
    rr=(bcue->d+bhit->d)/2.0;
    for( k=0; k<5; k++ ){
        ghost=vec_add(bhit->r,vec_add(vec_scale(along,-rr*cos(phi[k])),vec_scale(perp,rr*sin(phi[k]))));
        if( ball_in_way_ign(0,ghost,balls,i) ) continue;
        if( k==0 ){
            n=ai_add_shot(list,n,ghost,0.20,0.06);
            n=ai_add_shot(list,n,ghost,0.35,0.05);
        } else {
            n=ai_add_shot(list,n,ghost,0.30,0.04);
            n=ai_add_shot(list,n,ghost,0.50,0.03);
        }
    }
    return n;
}

/***********************************************************************/

static int ai_gen_kick_shots( BallsType * balls, AIShot * list, int n, int i )
/* one-cushion escapes when the direct way to every legal ball is blocked:
   aim at the mirror image of ball i behind each of the four cushions.
   The cushion is no ideal mirror (the rebound flattens with speed), so a
   fan of aim corrections AND strengths is generated around each mirror
   point - the simulation picks the combination that really connects. */
{
    static const double dt_fan[7]={0.0,-0.08,0.08,-0.18,0.18,-0.30,0.30};
    static const double sfac[3]={1.0,0.85,1.15};
    BallType *bcue=&balls->ball[0], *bhit=&balls->ball[i];
    VMfloat mx=TABLE_W/2.0-bcue->d/2.0;
    VMfloat my=TABLE_L/2.0-bcue->d/2.0;
    VMfloat s, s2, lambda;
    VMvect t, t2, dirm, cpoint;
    int k, m, q;

    for( k=0; k<4; k++ ){
        t=bhit->r;
        switch( k ){
          case 0: t.x= 2.0*mx-t.x; break;
          case 1: t.x=-2.0*mx-t.x; break;
          case 2: t.y= 2.0*my-t.y; break;
          case 3: t.y=-2.0*my-t.y; break;
        }
        /* skip this rail if the way to the cushion is already blocked */
        dirm=vec_diff(t,bcue->r);
        switch( k ){
          case 0:  lambda=( mx-bcue->r.x)/dirm.x; break;
          case 1:  lambda=(-mx-bcue->r.x)/dirm.x; break;
          case 2:  lambda=( my-bcue->r.y)/dirm.y; break;
          default: lambda=(-my-bcue->r.y)/dirm.y; break;
        }
        cpoint=vec_add(bcue->r,vec_scale(dirm,lambda));
        cpoint.z=0.0;
        if( ball_in_way_ign(0,cpoint,balls,i) ) continue;

        s=0.3+0.15*vec_abs(dirm);
        if( s>0.9 ) s=0.9;
        for( m=0; m<7; m++ ){
            t2=t;
            if( k<2 ) t2.y+=dt_fan[m];   /* correct along the rail */
            else      t2.x+=dt_fan[m];
            for( q=0; q<3; q++ ){
                s2=s*sfac[q];
                if( s2>1.0 ) s2=1.0;
                n=ai_add_shot(list,n,t2,s2,0.02-0.002*(VMfloat)m-((q>0)?0.001:0.0));
            }
        }
    }
    return n;
}

/***********************************************************************/

static VMfloat ai_apply_strength_err( VMfloat s )
{
    s*=1.0+0.3*ai_err*(my_rand01()-0.5);
    if( s<0.08 ) s=0.08;
    if( s>1.0 ) s=1.0;
    return s;
}

/***********************************************************************/

static int ai_sim_candidates( BallsType * balls, BordersType * walls, AIShot * cand, int ncand,
                              int maxsims, VMfloat (*score)(BordersType *, void *), void * ctx,
                              AIShot * best, VMfloat * bestscore, int have )
/* simulate the most promising candidates and keep the best rated one */
{
    VMfloat sc;
    int i, nsim;

    qsort(cand,ncand,sizeof(AIShot),ai_shot_cmp);
    nsim=(ncand<maxsims)?ncand:maxsims;
    for( i=0; i<nsim; i++ ){
        BM_simulation_begin();
        ai_sim_shot(balls,walls,cand[i].target,cand[i].strength,cand[i].side,cand[i].vert);
        sc=score(walls,ctx);
        BM_simulation_end();
        if( !have || sc>*bestscore ){ have=1; *bestscore=sc; *best=cand[i]; }
    }
    return have;
}

/***********************************************************************/

static void ai_refine_spin( BallsType * balls, BordersType * walls,
                            VMfloat (*score)(BordersType *, void *), void * ctx,
                            AIShot * best, VMfloat * bestscore )
/* try draw/follow/side english variants of the chosen pot: same aim, but
   the cue ball ends up somewhere else - kept when the position rating
   after the simulated shot improves */
{
    static const double verts[4]={0.016,0.008,-0.008,-0.016};  /* >0 draw, <0 follow */
    static const double sides[2]={0.012,-0.012};
    AIShot var[6];
    int i, n=0;

    for( i=0; i<4; i++ ){
        var[n]=*best; var[n].vert=verts[i]; n++;
    }
    for( i=0; i<2; i++ ){
        var[n]=*best; var[n].side=sides[i]; n++;
    }
    ai_sim_candidates(balls,walls,var,n,n,score,ctx,best,bestscore,1);
}

static VMfloat ai_score_8ball_cb( BordersType * walls, void * ctx )
{
    return ai_score_8ball(walls,(struct Player *)ctx);
}

/***********************************************************************/

static VMfloat ai_score_9ball_cb( BordersType * walls, void * ctx )
{
    return ai_score_9ball(walls,*(int *)ctx);
}

/***********************************************************************/

static void ai_commit_shot( struct Player * pplayer, AIShot * best )
/* write strength and english of the chosen shot (with err noise) into the
   player - queue_shot() picks them up from there */
{
    VMfloat mag;

    pplayer->strength=ai_apply_strength_err(best->strength);
    pplayer->cue_x=best->side*(1.0+0.3*ai_err*(my_rand01()-0.5));
    pplayer->cue_y=best->vert*(1.0+0.3*ai_err*(my_rand01()-0.5));
    mag=sqrt(pplayer->cue_x*pplayer->cue_x+pplayer->cue_y*pplayer->cue_y);
    if( mag>AI_MAX_ENGLISH ){
        pplayer->cue_x*=AI_MAX_ENGLISH/mag;
        pplayer->cue_y*=AI_MAX_ENGLISH/mag;
    }
}

/***********************************************************************/

VMvect ai_get_stroke_dir_8ball( BallsType * balls, BordersType * walls, struct Player * pplayer )
{
    AIShot cand[AI_MAX_CAND];
    AIShot best;
    VMvect r_hit;
    VMfloat bestscore=0.0;
    int ncand=0, reachable=0, have=0;
    int i, n0;

    pplayer->cue_x=0.0;
    pplayer->cue_y=0.0;

    /* 1) pot candidates for every ball we are allowed to play */
    for( i=1; i<balls->nr; i++ ){
        if( ai_8ball_pottable(balls,i,pplayer->half_full) ){
            ncand=ai_gen_pot_shots(balls,walls,cand,ncand,i);
        }
    }
    have=ai_sim_candidates(balls,walls,cand,ncand,AI_POT_SIMS,
                           ai_score_8ball_cb,pplayer,&best,&bestscore,have);

    if( have && bestscore>=AI_SCORE_GOOD_POT ){
        /* 2a) a pot was found: try draw/follow/english for a better position */
        ai_refine_spin(balls,walls,ai_score_8ball_cb,pplayer,&best,&bestscore);
    } else {
        /* 2b) no reliable pot: search for a safety, or - if the direct way to
               every legal ball is blocked - for a one-cushion kick shot */
        ncand=0;
        for( i=1; i<balls->nr; i++ ){
            if( ai_8ball_pottable(balls,i,pplayer->half_full) ){
                n0=ncand;
                ncand=ai_gen_safety_shots(balls,cand,ncand,i);
                if( ncand>n0 ) reachable=1;
            }
        }
        if( !reachable ){
            for( i=1; i<balls->nr; i++ ){
                if( ai_8ball_pottable(balls,i,pplayer->half_full) ){
                    ncand=ai_gen_kick_shots(balls,cand,ncand,i);
                }
            }
        }
        have=ai_sim_candidates(balls,walls,cand,ncand,
                               reachable?AI_SAFETY_SIMS:AI_KICK_SIMS,
                               ai_score_8ball_cb,pplayer,&best,&bestscore,have);
    }

    if( have ){
        ai_commit_shot(pplayer,&best);
        r_hit=vec_diff(best.target,balls->ball[0].r);
    } else {
        /* nothing to try at all (should not happen): play any legal ball */
        n0=ind_ball_nr(8,balls);
        for( i=1; i<balls->nr; i++ ){
            if( ai_8ball_pottable(balls,i,pplayer->half_full) ){ n0=i; break; }
        }
        if( n0>=balls->nr ) n0=0;
        pplayer->strength=0.5;
        r_hit=vec_diff(balls->ball[n0].r,balls->ball[0].r);
    }

    r_hit=vec_add(r_hit,vec_scale(vec_xyz(my_rand01()-0.5,my_rand01()-0.5,my_rand01()-0.5),0.02*ai_err));
    r_hit.z=0.0;

    return vec_unit(r_hit);
}

/***********************************************************************/

VMvect ai_get_stroke_dir_9ball( BallsType * balls, BordersType * walls, struct Player * pplayer )
{
    AIShot cand[AI_MAX_CAND];
    AIShot best;
    VMvect r_hit;
    VMfloat bestscore=0.0;
    int ncand=0, have=0;
    int minind, minnr;

    pplayer->cue_x=0.0;
    pplayer->cue_y=0.0;

    minind=ai_lowest_ball(balls);
    if( minind==-1 ) minind=0;   /* should not happen */
    minnr=balls->ball[minind].nr;

    /* 1) pot candidates for the lowest ball */
    ncand=ai_gen_pot_shots(balls,walls,cand,ncand,minind);
    have=ai_sim_candidates(balls,walls,cand,ncand,AI_POT_SIMS,
                           ai_score_9ball_cb,&minnr,&best,&bestscore,have);

    if( have && bestscore>=AI_SCORE_GOOD_POT ){
        /* 2a) a pot was found: try draw/follow/english for a better position */
        ai_refine_spin(balls,walls,ai_score_9ball_cb,&minnr,&best,&bestscore);
    } else {
        /* 2b) no reliable pot: safety on the lowest ball, kick shot if it is
               not directly reachable */
        int kicks=0;
        ncand=ai_gen_safety_shots(balls,cand,0,minind);
        if( ncand==0 ){
            ncand=ai_gen_kick_shots(balls,cand,0,minind);
            kicks=1;
        }
        have=ai_sim_candidates(balls,walls,cand,ncand,
                               kicks?AI_KICK_SIMS:AI_SAFETY_SIMS,
                               ai_score_9ball_cb,&minnr,&best,&bestscore,have);
    }

    if( have ){
        ai_commit_shot(pplayer,&best);
        r_hit=vec_diff(best.target,balls->ball[0].r);
    } else {
        /* nothing to try at all (should not happen): aim at the lowest ball */
        pplayer->strength=0.5;
        r_hit=vec_diff(balls->ball[minind].r,balls->ball[0].r);
    }

    r_hit=vec_add(r_hit,vec_scale(vec_xyz(my_rand01()-0.5,my_rand01()-0.5,my_rand01()-0.5),0.02*ai_err));
    r_hit.z=0.0;

    return vec_unit(r_hit);
}

/***********************************************************************/

#define IS_RED(x) ( x==1 || x>=8 )

int snooker_ball_legal(int ball,struct Player *player)
{
    return ((IS_RED(ball) && player->snooker_on_red)
          ||(!IS_RED(ball) && !player->snooker_on_red && player->snooker_next_color<=1)
          ||(player->snooker_on_red==0 && ball==player->snooker_next_color && player->snooker_next_color>1));
}

/***********************************************************************/

#define CUE_OBJ_WEIGHT 0.2
#define OBJ_HOLE_WEIGHT 0.7
#define POINT_WEIGHT 0.3
#define CENTERHOLE_ANGLE_WEIGHT 1
#define CORNERHOLE_ANGLE_WEIGHT 1
#define ANGLE_WEIGHT 1

VMvect ai_get_stroke_dir_snooker( BallsType * balls, BordersType * walls, struct Player * pplayer )
{
    VMvect r_hit = vec_null();
    VMvect hole_aim = vec_null();
    VMvect hole_aim_def = vec_null();
    VMvect min_r_hit = vec_null();
    VMfloat angle, hole_angle,weight,corner_acentrism, d;
    BallType *bhit, *bcue;
    HoleType *hole;
    int minball=0;
    int minhole=-1;
    int i,j;
    int legal_ball=1;
    VMfloat minweight=12;

    //fprintf(stderr,"aiplayer: start player[%s]\n",pplayer->name);

    bcue = &balls->ball[0];
    for( i=balls->nr-1;i>=1; i-- ) if ( balls->ball[i].in_game ){
        if(snooker_ball_legal(i,pplayer)) {
            bhit = &balls->ball[i];
            for( j=0; j<walls->holenr; j++ ){
                hole = &walls->hole[j];
                if(j>1) {
                    hole_aim=hole->aim;
                    if(hole_aim.x>0) hole_aim.x-=0.007;
                    if(hole_aim.x<0) hole_aim.x+=0.007;
                    if(hole_aim.y>0) hole_aim.y-=0.007;
                    if(hole_aim.y<0) hole_aim.y+=0.007;
                } else {
                    hole_aim=vec_scale(hole->aim,1.02);
                }
                r_hit = vec_scale(vec_unit(vec_diff(bhit->r,hole_aim)),(bcue->d+bhit->d)/2.0);
                r_hit = vec_add(bhit->r,r_hit);
                if( !ball_in_way(0,r_hit,balls) && !ball_in_way(i,hole_aim,balls) ){
                    angle = fabs( vec_angle( vec_diff(r_hit,bcue->r), vec_diff(hole_aim,r_hit) ) );
                    hole_angle = vec_angle(vec_diff(hole_aim,bhit->r),vec_diff(vec_xyz(2*hole_aim.x,hole_aim.y,0),hole_aim));
                    corner_acentrism=fabs(hole_angle-M_PI/4);
                    /*fprintf(stderr,"aiplayer: ball:%d hole:%d raw hole_angle %f\n",balls->ball[i].nr,j, hole_angle*180/M_PI);*/
                    /* normalized to give an unweighted maximum of +- 2.5 per item */
                    weight = 0.64 * ANGLE_WEIGHT * pow(angle,3)
                             +(j<=1)*0.64 * CENTERHOLE_ANGLE_WEIGHT * pow(hole_angle,3)
                             +(j>1) * 5.0 * CORNERHOLE_ANGLE_WEIGHT * pow(corner_acentrism,3)
                             +0.04 * OBJ_HOLE_WEIGHT * vec_abssq(vec_diff(bhit->r,hole_aim))
                             +0.15 * CUE_OBJ_WEIGHT * vec_abs(vec_diff(bhit->r,bcue->r))
                             -1.5 * POINT_WEIGHT *(i>=2||i<=7?i-2:0) 
                             ; 
                    if(weight <minweight){ 
                        minball = i; minhole = j;  minweight = weight;hole_aim_def=hole_aim;
                        //fprintf(stderr,"aiplayer: ball:%d hole:%d\n",i,minhole);
                    }
                }
            }
        }
    }
    /*fprintf(stderr,"aiplayer: 1\n");*/

    if( minball==0 ) {
    /* no pottable ball found, hit any legal ball */
        for( i=balls->nr-1;i>=1 && minball==0;i--) if ( balls->ball[i].in_game ){
            if(snooker_ball_legal(i,pplayer)) {
                for(d=-(balls->ball[0].d-0.001);d<(balls->ball[0].d-0.001) && minball==0;d+=0.005) {
                   VMvect offset,new_r_hit;
                   bhit = &balls->ball[i];

                   r_hit = vec_diff(bhit->r,bcue->r);

                   offset=vec_scale(vec_unit(vec_rotate(r_hit,vec_xyz(0,0,M_PI/2))),d);
                   new_r_hit = vec_diff(vec_add(bhit->r,offset),bcue->r);
                   new_r_hit = vec_diff(new_r_hit,vec_scale(vec_unit(new_r_hit),balls->ball[i].d/1.99));
                   
                   if( !ball_in_way(0,vec_add(balls->ball[0].r,new_r_hit),balls)) {
                       //fprintf(stderr,"ball found\n");
                       minball=i;
                       min_r_hit=new_r_hit;

                   }
               }
            }
        }
    }

    if( minball==0 ) {
    /* no legal ball found, just play anything */
        legal_ball=0;
        minball=0;
        if (pplayer->snooker_on_red) {
           minball=2+my_rand(6);
        } else if (pplayer->snooker_on_red==0 && pplayer->snooker_next_color<2) {
           minball=7+my_rand(15);
           if(minball==21) minball=1;
        } else {
           minball=pplayer->snooker_next_color;
        }
    }
    /*fprintf(stderr,"aiplayer: 2\n");*/

    bhit = &balls->ball[minball];
    if(minhole!=-1){
        hole = &walls->hole[minhole];
        r_hit = vec_scale(vec_unit(vec_diff(bhit->r,hole_aim_def)),(bcue->d+bhit->d)/2.0);
        r_hit = vec_diff(vec_add(bhit->r,r_hit),bcue->r);
    } else {  /* no proper ball found */
        if(legal_ball==0) {
           //fprintf(stderr,"aiplayer: no proper ball found\n");
           r_hit = vec_diff(bhit->r,bcue->r);
        } else {
           //fprintf(stderr,"aiplayer: can hit legal ball : %d\n",minball);
           r_hit=min_r_hit;
        }
    }

    r_hit=vec_add(r_hit,vec_scale(vec_xyz(my_rand01()-0.5,my_rand01()-0.5,my_rand01()-0.5),0.02*ai_err)); ///vec_abs(r_hit)));

    //(fprintf(stderr,"aiplayer: done\n");
    return vec_unit(r_hit);
}
#undef IS_RED

/***********************************************************************/

VMvect ai_get_stroke_dir_carambol( BallsType * balls, BordersType * walls, struct Player * pplayer ) {
	   // walls not used, but needed as set as function pointer
	   // Don't optimize this
#define CUE_BALL_IND (pplayer->cue_ball)
    int i,j,k;
    int foundshot=0;
    VMvect wc;    /* way of cueball to objectball 1 */
    VMvect w12;   /* way of cueball from objectball 1 to objectball 2 */
    VMvect d12;   /* vec from objectball 1 to objectball 2 */
    VMvect p1;    /* vec from ball1 to cueball at hit */
    VMvect min_wc;
    VMfloat min_angle=M_PI;
    VMfloat th;

    for(i=0;i<balls->nr;i++) if(i!=CUE_BALL_IND){             /* i = objectball 1 */
        for( j=0 ; j<balls->nr ; j++ ){                       /* j = objectball 2 */
            if(j!=CUE_BALL_IND && j!=i) break;
        }
        //fprintf(stderr,"ai_get_stroke_dir_carambol:i=%d,j=%d\n",i,j);
        d12 = vec_diff( balls->ball[j].r, balls->ball[i].r );
        th = acos(BALL_D/vec_abs(d12));
        for(k=0;k<2;k++){  /* the two possible tangents */
            p1  = vec_scale( vec_unit( vec_rotate(d12,vec_xyz(0,0,(k==0)?th:-th)) ), BALL_D );
            wc  = vec_diff( vec_add(balls->ball[i].r,p1), balls->ball[CUE_BALL_IND].r );
            w12 = vec_diff( balls->ball[j].r, vec_add(balls->ball[i].r,p1) );
            if( vec_mul(wc,w12)>0.0 && vec_mul(wc,p1)<0.0 ){  /* conditions for possible shot */
                foundshot=1;
                if( vec_angle( wc, w12 ) < min_angle ){
                    min_angle = vec_angle( wc, w12 );
                    min_wc = wc;
                }
            }
        }
    }
    if(foundshot){
        return vec_unit(min_wc);
    } else {
        return vec_unit( vec_diff(balls->ball[(CUE_BALL_IND+1+(rand()%2))%3].r, balls->ball[CUE_BALL_IND].r) );
    }
#undef CUE_BALL_IND
}

/***********************************************************************/

void setfunc_ai_get_stroke_dir(VMvect (*func)( BallsType * balls, BordersType * walls, struct Player * pplayer ))
{
    ai_get_stroke_dir=func;
}
