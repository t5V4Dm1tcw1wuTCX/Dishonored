#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <d3d9.h>
#include <cstdio>
#include <cmath>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_dx9.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "game.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

/* --- vars --- */
static bool bEsp       = true;
static bool bBox       = true;
static bool bBox3d     = false;
static bool bSkel      = true;
static bool bNames     = true;
static bool bDist      = true;
static bool bHpBar     = true;
static bool bHead      = true;
static bool bSnap      = false;
static bool bMenu      = true;
static bool bOnlyEnemy = false;
static bool bItems     = false;
static bool bItemBox   = true;
static bool bItemName  = true;
static bool bItemDist  = true;
static float fItemDist = 50.f;
// item filters: 0=rune, 1=bonecharm, 2=health, 3=mana, 4=key, 5=loot, 6=weapon, 7=note, 8=other
static bool itemFilter[9] = {true,true,true,true,true,true,true,true,true};
static float cItem[4] = {1.f, .85f, 0.f, 1.f};
static bool bInteract   = false;
static float fInteractDist = 50.f;
static float cInteract[4] = {0.f, 1.f, .8f, 1.f};
static bool bCrosshair  = true;
static bool bTeleport   = false;
static float fTpHeight  = 5.f;
static bool bNoFallDmg  = false;
static bool bNoclip     = false;
static float fNoclipSpeed = 15.f;
static float fMaxDist  = 150.f;
static bool bMagicBullet = false;
static float fBoxThk   = 1.5f;
static float fSkelThk  = 1.5f;
static int iBoxMode    = 0;
static int iSnapFrom   = 0;

static float cHostile[4]  = {1.f, .15f, .15f, 1.f};
static float cSuspect[4]  = {1.f, .7f, 0.f, 1.f};
static float cPassive[4]  = {.2f, .6f, 1.f, 1.f};
static float cFriend[4]   = {.3f, 1.f, .3f, 1.f};
static float cBones[4]    = {1.f, 1.f, 0.f, .8f};
static float cSnap[4]     = {1.f, 1.f, 1.f, .5f};

#define CFG_PATH "esp.cfg"
static char cfgMsg[48] = "";
static DWORD cfgTime = 0;

static void cfgSave() {
    FILE* fp = fopen(CFG_PATH, "w");
    if (!fp) { strcpy(cfgMsg, "err: cant write cfg"); cfgTime = GetTickCount(); return; }
    fprintf(fp, "esp=%d\nbox=%d\nbox3d=%d\nskel=%d\nnames=%d\ndist=%d\n", bEsp, bBox, bBox3d, bSkel, bNames, bDist);
    fprintf(fp, "hpbar=%d\nhead=%d\nsnap=%d\nsnapfrom=%d\nonlyenemy=%d\n", bHpBar, bHead, bSnap, iSnapFrom, bOnlyEnemy);
    fprintf(fp, "maxdist=%.1f\nboxthk=%.2f\nskelthk=%.2f\nboxmode=%d\n", fMaxDist, fBoxThk, fSkelThk, iBoxMode);
    fprintf(fp, "ch=%.3f,%.3f,%.3f,%.3f\n", cHostile[0], cHostile[1], cHostile[2], cHostile[3]);
    fprintf(fp, "cu=%.3f,%.3f,%.3f,%.3f\n", cSuspect[0], cSuspect[1], cSuspect[2], cSuspect[3]);
    fprintf(fp, "cp=%.3f,%.3f,%.3f,%.3f\n", cPassive[0], cPassive[1], cPassive[2], cPassive[3]);
    fprintf(fp, "cf=%.3f,%.3f,%.3f,%.3f\n", cFriend[0], cFriend[1], cFriend[2], cFriend[3]);
    fprintf(fp, "cb=%.3f,%.3f,%.3f,%.3f\n", cBones[0], cBones[1], cBones[2], cBones[3]);
    fprintf(fp, "cs=%.3f,%.3f,%.3f,%.3f\n", cSnap[0], cSnap[1], cSnap[2], cSnap[3]);
    fprintf(fp, "items=%d\nitembox=%d\nitemname=%d\nitemdist=%d\nitemdistf=%.1f\n", bItems, bItemBox, bItemName, bItemDist, fItemDist);
    fprintf(fp, "ci=%.3f,%.3f,%.3f,%.3f\n", cItem[0], cItem[1], cItem[2], cItem[3]);
    fprintf(fp, "ifilter=%d,%d,%d,%d,%d,%d,%d,%d,%d\n", itemFilter[0],itemFilter[1],itemFilter[2],itemFilter[3],itemFilter[4],itemFilter[5],itemFilter[6],itemFilter[7],itemFilter[8]);
    fprintf(fp, "interact=%d\ninteractdist=%.1f\ncrosshair=%d\nteleport=%d\ntpheight=%.1f\nnofalldmg=%d\nnoclipspd=%.1f\nmagicbullet=%d\n", bInteract, fInteractDist, bCrosshair, bTeleport, fTpHeight, bNoFallDmg, fNoclipSpeed, bMagicBullet);
    fprintf(fp, "cint=%.3f,%.3f,%.3f,%.3f\n", cInteract[0], cInteract[1], cInteract[2], cInteract[3]);
    fclose(fp);
    strcpy(cfgMsg, "saved"); cfgTime = GetTickCount();
}

static void cfgLoad() {
    FILE* fp = fopen(CFG_PATH, "r");
    if (!fp) return;
    char ln[256];
    while (fgets(ln, 256, fp)) {
        int v; float f;
        if (sscanf(ln, "esp=%d", &v)==1) bEsp=v;
        else if (sscanf(ln, "box=%d", &v)==1) bBox=v;
        else if (sscanf(ln, "box3d=%d", &v)==1) bBox3d=v;
        else if (sscanf(ln, "skel=%d", &v)==1) bSkel=v;
        else if (sscanf(ln, "names=%d", &v)==1) bNames=v;
        else if (sscanf(ln, "dist=%d", &v)==1) bDist=v;
        else if (sscanf(ln, "hpbar=%d", &v)==1) bHpBar=v;
        else if (sscanf(ln, "head=%d", &v)==1) bHead=v;
        else if (sscanf(ln, "snap=%d", &v)==1) bSnap=v;
        else if (sscanf(ln, "snapfrom=%d", &v)==1) iSnapFrom=v;
        else if (sscanf(ln, "onlyenemy=%d", &v)==1) bOnlyEnemy=v;
        else if (sscanf(ln, "maxdist=%f", &f)==1) fMaxDist=f;
        else if (sscanf(ln, "boxthk=%f", &f)==1) fBoxThk=f;
        else if (sscanf(ln, "skelthk=%f", &f)==1) fSkelThk=f;
        else if (sscanf(ln, "boxmode=%d", &v)==1) iBoxMode=v;
        else if (sscanf(ln, "ch=%f,%f,%f,%f", &cHostile[0],&cHostile[1],&cHostile[2],&cHostile[3])==4) {}
        else if (sscanf(ln, "cu=%f,%f,%f,%f", &cSuspect[0],&cSuspect[1],&cSuspect[2],&cSuspect[3])==4) {}
        else if (sscanf(ln, "cp=%f,%f,%f,%f", &cPassive[0],&cPassive[1],&cPassive[2],&cPassive[3])==4) {}
        else if (sscanf(ln, "cf=%f,%f,%f,%f", &cFriend[0],&cFriend[1],&cFriend[2],&cFriend[3])==4) {}
        else if (sscanf(ln, "cb=%f,%f,%f,%f", &cBones[0],&cBones[1],&cBones[2],&cBones[3])==4) {}
        else if (sscanf(ln, "cs=%f,%f,%f,%f", &cSnap[0],&cSnap[1],&cSnap[2],&cSnap[3])==4) {}
        else if (sscanf(ln, "items=%d", &v)==1) bItems=v;
        else if (sscanf(ln, "itembox=%d", &v)==1) bItemBox=v;
        else if (sscanf(ln, "itemname=%d", &v)==1) bItemName=v;
        else if (sscanf(ln, "itemdist=%d", &v)==1) bItemDist=v;
        else if (sscanf(ln, "itemdistf=%f", &f)==1) fItemDist=f;
        else if (sscanf(ln, "ci=%f,%f,%f,%f", &cItem[0],&cItem[1],&cItem[2],&cItem[3])==4) {}
        else if (strncmp(ln, "ifilter=", 8)==0) { int a[9]; if(sscanf(ln+8,"%d,%d,%d,%d,%d,%d,%d,%d,%d",&a[0],&a[1],&a[2],&a[3],&a[4],&a[5],&a[6],&a[7],&a[8])==9) for(int i=0;i<9;i++) itemFilter[i]=a[i]; }
        else if (sscanf(ln, "interact=%d", &v)==1) bInteract=v;
        else if (sscanf(ln, "interactdist=%f", &f)==1) fInteractDist=f;
        else if (sscanf(ln, "crosshair=%d", &v)==1) bCrosshair=v;
        else if (sscanf(ln, "teleport=%d", &v)==1) bTeleport=v;
        else if (sscanf(ln, "tpheight=%f", &f)==1) fTpHeight=f;
        else if (sscanf(ln, "nofalldmg=%d", &v)==1) bNoFallDmg=v;
        else if (sscanf(ln, "noclipspd=%f", &f)==1) fNoclipSpeed=f;
        else if (sscanf(ln, "magicbullet=%d", &v)==1) bMagicBullet=v;
        else if (sscanf(ln, "cint=%f,%f,%f,%f", &cInteract[0],&cInteract[1],&cInteract[2],&cInteract[3])==4) {}
    }
    fclose(fp);
    strcpy(cfgMsg, "loaded"); cfgTime = GetTickCount();
}

static inline ImU32 c4(const float* c) {
    return IM_COL32((int)(c[0]*255),(int)(c[1]*255),(int)(c[2]*255),(int)(c[3]*255));
}

static void txtOutline(ImDrawList* d, float x, float y, ImU32 col, const char* t) {
    ImU32 bg = IM_COL32(0,0,0,200);
    d->AddText(ImVec2(x-1,y), bg, t); d->AddText(ImVec2(x+1,y), bg, t);
    d->AddText(ImVec2(x,y-1), bg, t); d->AddText(ImVec2(x,y+1), bg, t);
    d->AddText(ImVec2(x,y), col, t);
}

static void cornerBox(ImDrawList* d, float x1, float y1, float x2, float y2, ImU32 c, float t) {
    float w = (x2-x1)*0.25f, h = (y2-y1)*0.25f;
    d->AddLine({x1,y1}, {x1+w,y1}, c, t); d->AddLine({x1,y1}, {x1,y1+h}, c, t);
    d->AddLine({x2,y1}, {x2-w,y1}, c, t); d->AddLine({x2,y1}, {x2,y1+h}, c, t);
    d->AddLine({x1,y2}, {x1+w,y2}, c, t); d->AddLine({x1,y2}, {x1,y2-h}, c, t);
    d->AddLine({x2,y2}, {x2-w,y2}, c, t); d->AddLine({x2,y2}, {x2,y2-h}, c, t);
}

static int itemCategory(const std::string& name) {
    if (name == "Rune") return 0;
    if (name == "Bone Charm") return 1;
    if (name == "Health Elixir") return 2;
    if (name == "Mana Elixir") return 3;
    if (name == "Key") return 4;
    if (name == "Loot") return 5;
    if (name == "Weapon") return 6;
    if (name == "Note" || name == "Audio Log") return 7;
    return 8;
}

/* main render */
static void render(Game::GameReader& g, float sw, float sh)
{
    if (!bEsp) return;

    Game::Vec3 cp; Game::Rotator cr;
    if (!g.GetCameraData(cp, cr)) return;

    Game::Matrix4 vpm;
    g.BuildVPM(cp, cr, sw, sh, vpm);
    auto ents = g.GatherActors(cp, fMaxDist);
    auto* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect({0,0}, {sw,sh});

    ImU32 cH = c4(cHostile), cU = c4(cSuspect), cP = c4(cPassive), cFr = c4(cFriend);
    ImU32 cb = c4(cBones), cs = c4(cSnap);

    for (auto& e : ents)
    {
        if (bOnlyEnemy && e.relation != 2) continue;

        ImU32 col;
        if (e.relation == 0) col = cFr;       // friendly (green) - not attackable
        else if (e.relation == 1) col = cP;    // passive (blue) - civilian
        else if (e.aiState == Game::AI_Combat) col = cH;  // hostile in combat (red)
        else if (e.aiState == Game::AI_Suspicious) col = cU; // suspicious (orange)
        else col = cH;

        std::vector<bool> used(e.Bones.size(), false);
        for (auto& [a,b] : e.BoneConnections) {
            if (a < (int)used.size()) used[a] = true;
            if (b < (int)used.size()) used[b] = true;
        }
        if (e.headEndBoneIdx >= 0 && e.headEndBoneIdx < (int)used.size())
            used[e.headEndBoneIdx] = true;

        // bbox from bones
        float x0=sw, y0=sh, x1=0, y1=0;
        int np = 0;
        if (e.Bones.size() >= 2) {
            for (size_t i = 0; i < e.Bones.size(); i++) {
                if (!used[i]) continue;
                auto& bn = e.Bones[i];
                if (bn.X==0.f && bn.Y==0.f && bn.Z==0.f) continue;
                Game::Vec3 s;
                if (Game::WorldToScreen(bn, vpm, sw, sh, s)) {
                    if (s.X<x0) x0=s.X; if (s.Y<y0) y0=s.Y;
                    if (s.X>x1) x1=s.X; if (s.Y>y1) y1=s.Y;
                    np++;
                }
            }
        }
        bool gotBox = np >= 2;
        float pd = 8.f;

        // 2d box
        if (bBox) {
            if (gotBox) {
                if (iBoxMode == 1)
                    cornerBox(dl, x0-pd, y0-pd, x1+pd, y1+pd, col, fBoxThk);
                else {
                    dl->AddRect({x0-pd-1,y0-pd-1}, {x1+pd+1,y1+pd+1}, IM_COL32(0,0,0,140), 0, 0, fBoxThk+1.f);
                    dl->AddRect({x0-pd,y0-pd}, {x1+pd,y1+pd}, col, 0, 0, fBoxThk);
                }
            } else {
                Game::Vec3 ft = e.Location, hd = e.Location;
                hd.Z += 180.f;
                Game::Vec3 sH, sF;
                if (Game::WorldToScreen(hd, vpm, sw, sh, sH) &&
                    Game::WorldToScreen(ft, vpm, sw, sh, sF)) {
                    float h = fabsf(sF.Y - sH.Y), w = h * 0.45f;
                    float bx0 = sH.X-w*.5f, by0 = sH.Y, bx1 = sH.X+w*.5f, by1 = sF.Y;
                    if (iBoxMode == 1) cornerBox(dl, bx0, by0, bx1, by1, col, fBoxThk);
                    else {
                        dl->AddRect({bx0-1,by0-1},{bx1+1,by1+1}, IM_COL32(0,0,0,140), 0, 0, fBoxThk+1.f);
                        dl->AddRect({bx0,by0},{bx1,by1}, col, 0, 0, fBoxThk);
                    }
                }
            }
        }

        // 3d box
        if (bBox3d && e.Bones.size() >= 2) {
            float ax,ay,az,bx,by,bz; bool first=true;
            for (size_t i=0;i<e.Bones.size();i++) {
                if (!used[i]) continue;
                auto& b = e.Bones[i];
                if (b.X==0.f&&b.Y==0.f&&b.Z==0.f) continue;
                if (first) { ax=bx=b.X; ay=by=b.Y; az=bz=b.Z; first=false; continue; }
                if(b.X<ax)ax=b.X; if(b.X>bx)bx=b.X;
                if(b.Y<ay)ay=b.Y; if(b.Y>by)by=b.Y;
                if(b.Z<az)az=b.Z; if(b.Z>bz)bz=b.Z;
            }
            if (!first) {
                ax-=15;ay-=15;az-=10;bx+=15;by+=15;bz+=10;
                Game::Vec3 cn[8] = {
                    {ax,ay,az},{bx,ay,az},{bx,by,az},{ax,by,az},
                    {ax,ay,bz},{bx,ay,bz},{bx,by,bz},{ax,by,bz}
                };
                Game::Vec3 sc[8]; bool ok=true;
                for (int i=0;i<8;i++) if (!Game::WorldToScreen(cn[i],vpm,sw,sh,sc[i])) {ok=false;break;}
                if (ok) {
                    int ed[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
                    for (auto& ee : ed)
                        dl->AddLine({sc[ee[0]].X,sc[ee[0]].Y},{sc[ee[1]].X,sc[ee[1]].Y}, col, fBoxThk);
                }
            }
        }

        // project skeleton bones
        std::vector<Game::Vec3> sb;
        std::vector<bool> bv;
        if (e.Bones.size() >= 2) {
            sb.resize(e.Bones.size());
            bv.resize(e.Bones.size(), false);
            for (size_t i=0; i<e.Bones.size(); i++) {
                auto& bn = e.Bones[i];
                if (bn.X==0.f&&bn.Y==0.f&&bn.Z==0.f) continue;
                bv[i] = Game::WorldToScreen(bn, vpm, sw, sh, sb[i]);
            }
        }

        if (bSkel && sb.size() >= 2) {
            for (auto& [a,b] : e.BoneConnections)
                if (a<(int)sb.size() && b<(int)sb.size() && bv[a] && bv[b])
                    dl->AddLine({sb[a].X,sb[a].Y},{sb[b].X,sb[b].Y}, cb, fSkelThk);
        }

        // head
        if (bHead && sb.size() >= 2) {
            int hi = e.headBoneIdx, he = e.headEndBoneIdx;
            if (hi>=0 && hi<(int)sb.size() && bv[hi] && he>=0 && he<(int)sb.size() && bv[he]) {
                float cx = (sb[hi].X+sb[he].X)*.5f, cy = (sb[hi].Y+sb[he].Y)*.5f;
                float dx = sb[he].X-sb[hi].X, dy = sb[he].Y-sb[hi].Y;
                float r = sqrtf(dx*dx+dy*dy) * .5f;
                if (r < 3.f) r = 3.f;
                dl->AddCircle({cx,cy}, r+1.f, IM_COL32(0,0,0,140), 18, fSkelThk+1.f);
                dl->AddCircle({cx,cy}, r, cb, 18, fSkelThk);
            }
        }

        // snaplines
        if (bSnap && gotBox) {
            float sx = sw*.5f;
            float sy = (iSnapFrom==2) ? 0.f : (iSnapFrom==1) ? sh*.5f : sh;
            dl->AddLine({sx,sy}, {(x0+x1)*.5f, y1+pd}, cs, 1.f);
        }

        // name + dist
        if (bNames || bDist) {
            Game::Vec3 scr; bool ok = false;
            int he = e.headEndBoneIdx;
            if (he>=0 && he<(int)e.Bones.size() && !(e.Bones[he].X==0.f&&e.Bones[he].Y==0.f&&e.Bones[he].Z==0.f))
                ok = Game::WorldToScreen(e.Bones[he], vpm, sw, sh, scr);
            if (!ok) {
                Game::Vec3 hp = e.Location; hp.Z += 180.f;
                ok = Game::WorldToScreen(hp, vpm, sw, sh, scr);
            }
            if (ok) {
                char buf[256];
                const char* stateTag = "";
                if (e.relation == 0) stateTag = "[F]";
                else if (e.relation == 1) stateTag = "[P]";
                else if (e.aiState == Game::AI_Combat) stateTag = "[!]";
                else if (e.aiState == Game::AI_Suspicious) stateTag = "[?]";

                if (bNames && bDist) sprintf(buf, "%s%s [%.0fm]", e.Name.c_str(), stateTag, e.Distance);
                else if (bNames) sprintf(buf, "%s%s", e.Name.c_str(), stateTag);
                else sprintf(buf, "%s[%.0fm]", stateTag, e.Distance);
                auto ts = ImGui::CalcTextSize(buf);
                txtOutline(dl, scr.X - ts.x*.5f, scr.Y - ts.y - 14.f, col, buf);
            }
        }

        // hp bar
        if (bHpBar && gotBox) {
            float by = y1+pd+3.f, bh = 3.f;
            float bl = x0-pd, br = x1+pd;
            float fw = (br-bl) * e.healthPct;
            ImU32 hc = IM_COL32((int)(255*(1.f-e.healthPct)), (int)(255*e.healthPct), 0, 255);
            dl->AddRectFilled({bl-1,by-1},{br+1,by+bh+1}, IM_COL32(0,0,0,180));
            dl->AddRectFilled({bl,by},{bl+fw,by+bh}, hc);
        }
    }

    // crosshair on closest NPC to center
    if (bCrosshair && !ents.empty()) {
        float cx = sw * 0.5f, cy = sh * 0.5f;
        float bestD = 9999.f;
        Game::Vec3 bestScr;
        bool found = false;
        for (auto& e : ents) {
            if (bOnlyEnemy && e.relation != 2) continue;
            Game::Vec3 hp = e.Location; hp.Z += 90.f;
            Game::Vec3 scr;
            if (!Game::WorldToScreen(hp, vpm, sw, sh, scr)) continue;
            float dx = scr.X - cx, dy = scr.Y - cy;
            float d = sqrtf(dx*dx + dy*dy);
            if (d < bestD) { bestD = d; bestScr = scr; found = true; }
        }
        if (found && bestD < 200.f) {
            ImU32 xc = IM_COL32(255, 50, 50, 220);
            float sz = 10.f;
            dl->AddLine({bestScr.X - sz, bestScr.Y}, {bestScr.X + sz, bestScr.Y}, xc, 2.f);
            dl->AddLine({bestScr.X, bestScr.Y - sz}, {bestScr.X, bestScr.Y + sz}, xc, 2.f);
            dl->AddCircle({bestScr.X, bestScr.Y}, sz * 0.8f, xc, 12, 1.5f);
        }
    }

    // item esp
    if (bItems) {
        auto items = g.GatherItems(cp, fItemDist);
        ImU32 ci = c4(cItem);
        for (auto& it : items) {
            int cat = itemCategory(it.Name);
            if (cat < 9 && !itemFilter[cat]) continue;

            // 3D box around item
            if (bItemBox) {
                float hw = it.BoxExtent.X, hd = it.BoxExtent.Y, hh = it.BoxExtent.Z;
                Game::Vec3 corners[8] = {
                    {it.Location.X-hw, it.Location.Y-hd, it.Location.Z-hh},
                    {it.Location.X+hw, it.Location.Y-hd, it.Location.Z-hh},
                    {it.Location.X+hw, it.Location.Y+hd, it.Location.Z-hh},
                    {it.Location.X-hw, it.Location.Y+hd, it.Location.Z-hh},
                    {it.Location.X-hw, it.Location.Y-hd, it.Location.Z+hh},
                    {it.Location.X+hw, it.Location.Y-hd, it.Location.Z+hh},
                    {it.Location.X+hw, it.Location.Y+hd, it.Location.Z+hh},
                    {it.Location.X-hw, it.Location.Y+hd, it.Location.Z+hh},
                };
                Game::Vec3 s[8];
                bool allOk = true;
                float minX=9999,minY=9999,maxX=-9999,maxY=-9999;
                for (int c = 0; c < 8; c++) {
                    if (!Game::WorldToScreen(corners[c], vpm, sw, sh, s[c])) { allOk = false; break; }
                    if (s[c].X < minX) minX = s[c].X;
                    if (s[c].Y < minY) minY = s[c].Y;
                    if (s[c].X > maxX) maxX = s[c].X;
                    if (s[c].Y > maxY) maxY = s[c].Y;
                }
                if (allOk) {
                    // draw 3D wireframe
                    for (int c = 0; c < 4; c++) {
                        dl->AddLine({s[c].X,s[c].Y}, {s[(c+1)%4].X,s[(c+1)%4].Y}, ci, 1.f);
                        dl->AddLine({s[c+4].X,s[c+4].Y}, {s[((c+1)%4)+4].X,s[((c+1)%4)+4].Y}, ci, 1.f);
                        dl->AddLine({s[c].X,s[c].Y}, {s[c+4].X,s[c+4].Y}, ci, 1.f);
                    }
                    // name + dist above box
                    char buf[128];
                    if (bItemName && bItemDist) sprintf(buf, "%s [%.0fm]", it.Name.c_str(), it.Distance);
                    else if (bItemName) sprintf(buf, "%s", it.Name.c_str());
                    else if (bItemDist) sprintf(buf, "[%.0fm]", it.Distance);
                    else buf[0] = 0;
                    if (buf[0]) {
                        auto ts = ImGui::CalcTextSize(buf);
                        float cx = (minX+maxX)*.5f;
                        txtOutline(dl, cx - ts.x*.5f, minY - ts.y - 2.f, ci, buf);
                    }
                }
            } else {
                // no box, just text at projected center
                Game::Vec3 scr;
                if (!Game::WorldToScreen(it.Location, vpm, sw, sh, scr)) continue;
                char buf[128];
                if (bItemName && bItemDist) sprintf(buf, "%s [%.0fm]", it.Name.c_str(), it.Distance);
                else if (bItemName) sprintf(buf, "%s", it.Name.c_str());
                else if (bItemDist) sprintf(buf, "[%.0fm]", it.Distance);
                else buf[0] = 0;
                if (buf[0]) {
                    auto ts = ImGui::CalcTextSize(buf);
                    txtOutline(dl, scr.X - ts.x*.5f, scr.Y - ts.y - 4.f, ci, buf);
                }
            }
        }
    }

    // interactable esp
    if (bInteract) {
        auto interacts = g.GatherInteractables(cp, fInteractDist);
        ImU32 ci2 = c4(cInteract);
        for (auto& ia : interacts) {
            float hw = ia.BoxExtent.X, hd = ia.BoxExtent.Y, hh = ia.BoxExtent.Z;
            Game::Vec3 corners[8] = {
                {ia.Location.X-hw, ia.Location.Y-hd, ia.Location.Z-hh},
                {ia.Location.X+hw, ia.Location.Y-hd, ia.Location.Z-hh},
                {ia.Location.X+hw, ia.Location.Y+hd, ia.Location.Z-hh},
                {ia.Location.X-hw, ia.Location.Y+hd, ia.Location.Z-hh},
                {ia.Location.X-hw, ia.Location.Y-hd, ia.Location.Z+hh},
                {ia.Location.X+hw, ia.Location.Y-hd, ia.Location.Z+hh},
                {ia.Location.X+hw, ia.Location.Y+hd, ia.Location.Z+hh},
                {ia.Location.X-hw, ia.Location.Y+hd, ia.Location.Z+hh},
            };
            Game::Vec3 s[8];
            bool allOk = true;
            float minX=9999,minY=9999,maxX=-9999,maxY=-9999;
            for (int c = 0; c < 8; c++) {
                if (!Game::WorldToScreen(corners[c], vpm, sw, sh, s[c])) { allOk = false; break; }
                if (s[c].X < minX) minX = s[c].X;
                if (s[c].Y < minY) minY = s[c].Y;
                if (s[c].X > maxX) maxX = s[c].X;
                if (s[c].Y > maxY) maxY = s[c].Y;
            }
            if (allOk) {
                for (int c = 0; c < 4; c++) {
                    dl->AddLine({s[c].X,s[c].Y}, {s[(c+1)%4].X,s[(c+1)%4].Y}, ci2, 1.f);
                    dl->AddLine({s[c+4].X,s[c+4].Y}, {s[((c+1)%4)+4].X,s[((c+1)%4)+4].Y}, ci2, 1.f);
                    dl->AddLine({s[c].X,s[c].Y}, {s[c+4].X,s[c+4].Y}, ci2, 1.f);
                }
                char buf[128];
                sprintf(buf, "%s [%.0fm]", ia.Name.c_str(), ia.Distance);
                auto ts = ImGui::CalcTextSize(buf);
                float cx = (minX+maxX)*.5f;
                txtOutline(dl, cx - ts.x*.5f, minY - ts.y - 2.f, ci2, buf);
            }
        }
    }

    dl->PopClipRect();
}

static void setupStyle() {
    auto& s = ImGui::GetStyle();
    s.WindowRounding = 6.f;
    s.FrameRounding = 3.f;
    s.GrabRounding = 3.f;
    s.PopupRounding = 3.f;
    s.ScrollbarRounding = 4.f;
    s.WindowPadding = {10,10};
    s.FramePadding = {6,3};
    s.ItemSpacing = {7,5};
    s.WindowBorderSize = 1.f;
    s.WindowTitleAlign = {0.5f,0.5f};

    auto* c = s.Colors;
    c[ImGuiCol_WindowBg]           = {0.07f,0.07f,0.11f,0.95f};
    c[ImGuiCol_TitleBg]            = {0.09f,0.04f,0.18f,1.f};
    c[ImGuiCol_TitleBgActive]      = {0.18f,0.07f,0.36f,1.f};
    c[ImGuiCol_Border]             = {0.28f,0.14f,0.46f,0.5f};
    c[ImGuiCol_FrameBg]            = {0.11f,0.09f,0.16f,1.f};
    c[ImGuiCol_FrameBgHovered]     = {0.18f,0.13f,0.27f,1.f};
    c[ImGuiCol_FrameBgActive]      = {0.22f,0.16f,0.34f,1.f};
    c[ImGuiCol_CheckMark]          = {0.65f,0.38f,1.f,1.f};
    c[ImGuiCol_SliderGrab]         = {0.5f,0.28f,0.8f,1.f};
    c[ImGuiCol_SliderGrabActive]   = {0.65f,0.38f,1.f,1.f};
    c[ImGuiCol_Button]             = {0.16f,0.1f,0.25f,1.f};
    c[ImGuiCol_ButtonHovered]      = {0.25f,0.16f,0.38f,1.f};
    c[ImGuiCol_ButtonActive]       = {0.32f,0.2f,0.5f,1.f};
    c[ImGuiCol_Header]             = {0.16f,0.1f,0.25f,1.f};
    c[ImGuiCol_HeaderHovered]      = {0.25f,0.16f,0.38f,1.f};
    c[ImGuiCol_HeaderActive]       = {0.32f,0.2f,0.5f,1.f};
    c[ImGuiCol_Separator]          = {0.28f,0.14f,0.46f,0.35f};
    c[ImGuiCol_Text]               = {0.9f,0.88f,0.94f,1.f};
    c[ImGuiCol_TextDisabled]       = {0.42f,0.38f,0.5f,1.f};
    c[ImGuiCol_PopupBg]            = {0.09f,0.07f,0.13f,0.96f};
    c[ImGuiCol_ScrollbarBg]        = {0.04f,0.03f,0.07f,0.5f};
    c[ImGuiCol_ScrollbarGrab]      = {0.28f,0.18f,0.42f,0.7f};
    c[ImGuiCol_ScrollbarGrabHovered]= {0.38f,0.26f,0.54f,0.8f};
    c[ImGuiCol_ScrollbarGrabActive] = {0.46f,0.32f,0.65f,0.8f};
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int)
{
    SetProcessDPIAware();

    WNDCLASSEXA wc={sizeof(wc)};
    wc.style = CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = "OverlayWnd";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST|WS_EX_TRANSPARENT|WS_EX_LAYERED,
        wc.lpszClassName, "", WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        0, 0, inst, 0);

    SetLayeredWindowAttributes(hwnd, RGB(0,0,0), 0, LWA_COLORKEY);
    MARGINS m = {-1};
    DwmExtendFrameIntoClientArea(hwnd, &m);
    ShowWindow(hwnd, SW_SHOWDEFAULT);

    auto pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return 1;

    D3DPRESENT_PARAMETERS pp={};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D16;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    pp.hDeviceWindow = hwnd;

    IDirect3DDevice9* dev = 0;
    if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev)))
    { pD3D->Release(); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = 0;
    setupStyle();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(dev);

    Game::GameReader game;
    cfgLoad();

    bool run = true;
    MSG msg;
    int tab = 0;

    while (run)
    {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (msg.message == WM_QUIT) run = false;
        }
        if (!run) break;

        // keys
        static bool ks[256]={};
        auto kp = [](int k, bool& s) { bool d=(GetAsyncKeyState(k)&0x8000)!=0; if(d&&!s){s=true;return true;} if(!d)s=false; return false; };
        if (kp(VK_INSERT, ks[VK_INSERT])) bEsp = !bEsp;
        if (kp(VK_HOME, ks[VK_HOME])) bMenu = !bMenu;
        if (kp(VK_END, ks[VK_END])) { run=false; break; }

        if (!game.IsValid()) {
            game.Detach();
            if (!game.Attach()) { Sleep(1000); continue; }
        }

        RECT gr = game.GetWindowRect_();
        int gw = gr.right-gr.left, gh = gr.bottom-gr.top;
        if (gw > 0 && gh > 0)
            SetWindowPos(hwnd, HWND_TOPMOST, gr.left, gr.top, gw, gh, SWP_NOACTIVATE);

        static int lw=0, lh=0;
        if (gw>0 && gh>0 && (gw!=lw||gh!=lh)) {
            lw=gw; lh=gh;
            ImGui_ImplDX9_InvalidateDeviceObjects();
            pp.BackBufferWidth=gw; pp.BackBufferHeight=gh;
            dev->Reset(&pp);
            ImGui_ImplDX9_CreateDeviceObjects();
        }

        float sw=(float)gw, sh=(float)gh;

        if (bTeleport && kp(VK_F5, ks[VK_F5])) {
            Game::Vec3 tpCam; Game::Rotator tpRot;
            if (game.GetCameraData(tpCam, tpRot))
                game.TeleportAboveTarget(tpCam, tpRot, sw, sh, fMaxDist, fTpHeight);
        }

        if (kp(VK_F7, ks[VK_F7])) bMagicBullet = !bMagicBullet;

        if (bMagicBullet && game.IsValid()) {
            Game::Vec3 mbCam; Game::Rotator mbRot;
            if (game.GetCameraData(mbCam, mbRot))
                game.MagicBullet(mbCam, mbRot, sw, sh);
        }

        if (kp(VK_F6, ks[VK_F6])) bNoclip = !bNoclip;

        // noclip fly mode
        if (bNoclip && game.IsValid()) {
            Game::Vec3 ncCam; Game::Rotator ncRot;
            if (game.GetCameraData(ncCam, ncRot)) {
                float fv = 0.f, rv = 0.f, uv = 0.f;
                if (GetAsyncKeyState('W') & 0x8000) fv += 1.f;
                if (GetAsyncKeyState('S') & 0x8000) fv -= 1.f;
                if (GetAsyncKeyState('D') & 0x8000) rv += 1.f;
                if (GetAsyncKeyState('A') & 0x8000) rv -= 1.f;
                if (GetAsyncKeyState(VK_SPACE) & 0x8000) uv += 1.f;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) uv -= 1.f;
                if (fv != 0.f || rv != 0.f || uv != 0.f) {
                    float spd = fNoclipSpeed;
                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) spd *= 2.f;
                    game.NoclipMove(ncRot, fv, rv, uv, spd);
                }
                else {
                    // keep still: zero velocity
                    uintptr_t lp = 0;
                    if (game.RPM(Game::Off::LocalPawn, lp) && lp) {
                        Game::Vec3 zero = {0.f, 0.f, 0.f};
                        game.WPMBytes(lp + 0x01B4, &zero, 12);
                    }
                }
            }
        }

        // no fall damage: clamp negative Z velocity every frame
        if (bNoFallDmg && game.IsValid()) {
            uintptr_t lp = 0;
            if (game.RPM(Game::Off::LocalPawn, lp) && lp) {
                float vz = 0.f;
                // Velocity.Z = Actor + 0x01B4 (Velocity FVector) + 0x08 (Z offset)
                if (game.RPM(lp + 0x01BC, vz) && vz < -100.f) {
                    vz = -100.f;
                    game.WPM(lp + 0x01BC, vz);
                }
            }
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        render(game, sw, sh);

        // top bar
        if (bEsp) {
            auto* fg = ImGui::GetForegroundDrawList();
            const char* bar = bNoclip
                ? "[HOME] Menu | [F6] Noclip ON | [F7] Aimbot | [END] Quit"
                : bMagicBullet
                ? "[HOME] Menu | [INS] Toggle | [F7] Aimbot ON | [END] Quit"
                : "[HOME] Menu | [INS] Toggle | [F7] Aimbot | [END] Quit";
            auto ts = ImGui::CalcTextSize(bar);
            float bx = (sw-ts.x)*.5f;
            fg->AddRectFilled({bx-8,2},{bx+ts.x+8,ts.y+6}, IM_COL32(0,0,0,140), 4.f);
            fg->AddText({bx,4}, IM_COL32(170,130,240,255), bar);
        }

        // menu
        if (bMenu) {
            LONG ex = GetWindowLongA(hwnd, GWL_EXSTYLE);
            if (ex & WS_EX_TRANSPARENT)
                SetWindowLongA(hwnd, GWL_EXSTYLE, ex & ~WS_EX_TRANSPARENT);

            ImGui::SetNextWindowSize({320, 390}, ImGuiCond_FirstUseEver);
            ImGui::Begin("Dishonored ESP##main", &bMenu, ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar);

            // tabs
            const char* tabs[] = {"ESP", "Items", "Style", "Cfg", "Misc"};
            for (int i=0;i<5;i++) {
                if (i) ImGui::SameLine();
                bool sel = (tab==i);
                if (sel) {
                    ImGui::PushStyleColor(ImGuiCol_Button, {0.32f,0.2f,0.5f,1.f});
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.38f,0.26f,0.56f,1.f});
                }
                if (ImGui::Button(tabs[i], {70,24})) tab=i;
                if (sel) ImGui::PopStyleColor(2);
            }
            ImGui::Separator();
            ImGui::Spacing();

            if (tab == 0) { // ESP
                ImGui::Checkbox("Enable ESP", &bEsp);
                ImGui::Spacing();
                ImGui::Checkbox("2D Box", &bBox);
                ImGui::Checkbox("3D Box", &bBox3d);
                ImGui::Checkbox("Skeleton", &bSkel);
                ImGui::Checkbox("Head", &bHead);
                ImGui::Checkbox("Names", &bNames);
                ImGui::Checkbox("Distance", &bDist);
                ImGui::Checkbox("HP Bar", &bHpBar);
                ImGui::Checkbox("Snaplines", &bSnap);
                ImGui::Checkbox("Crosshair", &bCrosshair);
                ImGui::Checkbox("Only Enemies", &bOnlyEnemy);
                if (bSnap) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(85);
                    const char* sp[] = {"Bottom","Center","Top"};
                    ImGui::Combo("##sf", &iSnapFrom, sp, 3);
                }
            }
            else if (tab == 1) { // Items
                ImGui::Checkbox("Enable Items", &bItems);
                ImGui::Spacing();
                ImGui::Checkbox("3D Box", &bItemBox);
                ImGui::Checkbox("Name", &bItemName);
                ImGui::Checkbox("Distance", &bItemDist);
                ImGui::SliderFloat("Item range", &fItemDist, 5.f, 200.f, "%.0f m");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Filters:");
                const char* fn[] = {"Runes","Bone Charms","Health Elixir","Mana Elixir",
                                    "Keys","Loot","Weapons","Notes/Logs","Other"};
                for (int i=0;i<9;i++) ImGui::Checkbox(fn[i], &itemFilter[i]);
                ImGui::Spacing();
                ImGui::ColorEdit4("Item color##c", cItem, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Checkbox("Interactables", &bInteract);
                ImGui::SliderFloat("Interact range", &fInteractDist, 5.f, 200.f, "%.0f m");
                ImGui::ColorEdit4("Interact color##c", cInteract, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                if (ImGui::Button("Unlock All Nearby", {150,22})) {
                    Game::Vec3 uc; Game::Rotator ur;
                    if (game.GetCameraData(uc, ur)) {
                        int n = game.UnlockAll(uc, fInteractDist);
                        sprintf(cfgMsg, "unlocked %d", n); cfgTime = GetTickCount();
                    }
                }
            }
            else if (tab == 2) { // Style
                const char* bm[] = {"Normal","Corners"};
                ImGui::Combo("Box type", &iBoxMode, bm, 2);
                ImGui::SliderFloat("Box thk", &fBoxThk, .5f, 4.f, "%.1f");
                ImGui::SliderFloat("Skel thk", &fSkelThk, .5f, 4.f, "%.1f");
                ImGui::Spacing();
                ImGui::ColorEdit4("Hostile##c", cHostile, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                ImGui::ColorEdit4("Suspect##c", cSuspect, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                ImGui::ColorEdit4("Passive##c", cPassive, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                ImGui::ColorEdit4("Friendly##c", cFriend, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                ImGui::ColorEdit4("Bones##c", cBones, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                ImGui::ColorEdit4("Snap##c", cSnap, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
            }
            else if (tab == 3) { // Cfg
                ImGui::SliderFloat("Max dist", &fMaxDist, 10.f, 500.f, "%.0f m");
                ImGui::Spacing();
                if (ImGui::Button("Save cfg", {90,22})) cfgSave();
                ImGui::SameLine();
                if (ImGui::Button("Load cfg", {90,22})) cfgLoad();
                if (cfgMsg[0] && (GetTickCount()-cfgTime) < 2500)
                    ImGui::TextColored({.4f,1.f,.4f,1.f}, "%s", cfgMsg);
                ImGui::Spacing();
                ImGui::TextDisabled("INSERT  toggle esp");
                ImGui::TextDisabled("HOME    toggle menu");
                ImGui::TextDisabled("F5      teleport above NPC");
                ImGui::TextDisabled("F6      toggle noclip/fly");
                ImGui::TextDisabled("F7      magic bullet aimbot");
                ImGui::TextDisabled("END     quit");
            }
            else if (tab == 4) { // Misc
                if (game.IsValid()) {
                    ImGui::TextColored({.3f,1.f,.3f,1.f}, "attached (pid %lu)", game.processId);
                } else {
                    ImGui::TextColored({1.f,.3f,.3f,1.f}, "waiting for game...");
                }
                ImGui::Text("fps: %.0f", io.Framerate);
                ImGui::Spacing();
                ImGui::Checkbox("No Fall Damage", &bNoFallDmg);
                ImGui::Checkbox("Noclip / Fly (F6)", &bNoclip);
                if (bNoclip) {
                    ImGui::SliderFloat("Fly speed", &fNoclipSpeed, 1.f, 100.f, "%.0f");
                    ImGui::TextDisabled("WASD=move Space=up Ctrl=down");
                }
                ImGui::Checkbox("Teleport (F5)", &bTeleport);
                if (bTeleport) {
                    ImGui::SliderFloat("TP height", &fTpHeight, 1.f, 50.f, "%.0f m");
                }
                ImGui::Checkbox("Magic Bullet / Aimbot (F7)", &bMagicBullet);
                ImGui::Spacing();
                if (ImGui::Button("Dump Items")) { game.DumpAllItems(); strcpy(cfgMsg, "item_dump.txt written"); cfgTime = GetTickCount(); }
                ImGui::Text("cam: %s", game.camDebug.c_str());
                if (game.parentIndexOffset >= 0)
                    ImGui::Text("bone off: 0x%02X", game.parentIndexOffset);
                if (!game.probeLog.empty() && ImGui::CollapsingHeader("probe log"))
                    ImGui::TextUnformatted(game.probeLog.c_str());
            }

            ImGui::End();
        } else {
            LONG ex = GetWindowLongA(hwnd, GWL_EXSTYLE);
            if (!(ex & WS_EX_TRANSPARENT))
                SetWindowLongA(hwnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
        }

        ImGui::EndFrame();
        dev->Clear(0, 0, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0,0,0,0), 1.f, 0);
        if (dev->BeginScene() >= 0) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            dev->EndScene();
        }
        HRESULT hr = dev->Present(0,0,0,0);
        if (hr == D3DERR_DEVICELOST && dev->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
            ImGui_ImplDX9_InvalidateDeviceObjects();
            dev->Reset(&pp);
            ImGui_ImplDX9_CreateDeviceObjects();
        }
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    dev->Release();
    pD3D->Release();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, inst);
    game.Detach();
    return 0;
}
