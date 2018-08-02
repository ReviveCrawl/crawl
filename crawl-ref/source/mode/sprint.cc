#include "../build/AppHdr.h"

#include "sprint.h"

#include "../sys/externs.h"
#include "../map/maps.h"
#include "../mon/mon-util.h"
#include "../mon/monster.h"
#include "../io/mpr.h"
#include "../player/player.h"
#include "../sys/religion.h"
#include "../random/random.h"

int sprint_modify_exp(int exp)
{
    return exp * 9;
}

int sprint_modify_exp_inverse(int exp)
{
    return div_rand_round(exp, 9);
}

int sprint_modify_piety(int piety)
{
    if (you_worship(GOD_OKAWARU))
        return piety;
    return piety * 9;
}



