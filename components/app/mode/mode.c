#include "headfile.h"

// 闂傚倷绀侀幖顐も偓姘卞厴瀹曡瀵奸弶鎴犵暰婵炶揪绲块幊鎾舵閻愮儤鐓曢柡鍥ュ妺缁ㄤ粙鏌?闂傚倸鍊风欢锟犲磻閸曨垁鍥箥椤旂懓浜炬慨妯稿劚婵″ジ鎽堕敐澶嬬厓闁靛鍎抽敍宥囩棯椤撴稑浜?闂傚倷鐒︾€笛呯矙閹达附鍋嬮柟鎷屽焽閳ь剙鎳橀、鏇㈡晜閽樺澹嗛梻浣告惈鐠囩偤宕熼鍏煎創闂佽崵鍠愮划搴㈡櫠濡ゅ懎绠伴柛娑橈攻濞呯娀鏌ｅΟ鑲╁笡闁稿鏅濋埀顒€鍘滈崑鎾绘煕閺囥劌澧柕鍡楀暞缁绘盯骞嬮悙鏉戠濡炪値鍙庨崢鎯?
bool in_select = false;
// 闂備浇宕垫慨鎶芥倿閿曗偓椤灝螣閼测晝顦悗骞垮劚椤︻垳鈧艾顦甸弻宥堫檨闁告挾鍠庨悾鐑藉Ψ閿旇棄鍔呴梺闈涚箚閸撴繈宕曢妷锔剧閺夊牆澧界粙鑽ょ磼椤旇偐孝妞ゎ亜鍟村畷鎺楁倷缁瀚介梺鍝勵槸閻楀棙鏅堕悾宀€鐭嗛柛鏇ㄥ墯閸欏繘鏌ㄥ┑鍡涱€楀ù婊呭仦閵囧嫰顢曢敐搴㈢杹闂?
uint32_t last_action_time = 0; 
static bool ignore_first_long_press_after_wake = false;

extern TaskHandle_t sensor_task_handle;
extern TaskHandle_t sync_task_handle;

static void app_mode_set(ui_mode_e next_mode)
{
    // Radio owns background resources, so entering/leaving that page also drives player lifecycle.
    if (mode == MODE_RADIO && next_mode != MODE_RADIO) {
        audio_player_exit_radio_mode();
    }
    if (next_mode != MODE_SETTING) {
        setting_ui_reset_state();
    }
    mode = next_mode;
    if (mode == MODE_RADIO) {
        audio_player_enter_radio_mode();
    } else if (mode == MODE_SETTING) {
        setting_ui_reset_state();
    }
}

void key_scan(void)
{
    key_event_e event = key_get_event(KEY_USER);
 
    const int main_app_count = sizeof(main_app_list) / sizeof(main_app_list[0]);
    const int sub_game_count = sizeof(sub_game_list) / sizeof(sub_game_list[0]);

    if (event != KEY_EVENT_NONE) {
        // 闂傚倷绀侀幉锟犳偡椤栨稓顩查柨婵嗩槶閳ь剙鎳橀弫鍌炴倷椤掆偓閺嬪倿姊洪幐搴㈢５闁稿鎸鹃幉鎼佸级閸喒鍋撻崷顓犵焿鐎广儱顦獮銏＄箾閸℃ê鐏ョ憸鏉款儔濮婃椽宕烽褏鍔稿銈忕細閸楀啿鐣疯ぐ鎺懳╅柍鍝勫€稿▓宀勬⒑閸涘﹦鈽夐柣掳鍔岃闁规壆澧楅悡娆戠磼鐎ｎ偒鍎ユ俊鐙欏懐纾奸弶鍫涘妿缁犵偤鏌＄仦鍊熷妞ゆ挸銈稿畷鍫曞煛閸屾壕鍋撻銏♀拺缂佸灏呴弨濠氭偨椤栨粌鏋涙い銏★耿楠炴牗鎷呴崫銉ф綁?
        last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
    if (ignore_first_long_press_after_wake) {
        if (event == KEY_EVENT_SHORT) {
            ignore_first_long_press_after_wake = false;
        } else if (event == KEY_EVENT_LONG) {
            ignore_first_long_press_after_wake = false;
            return;
        }
    }



    if (event == KEY_EVENT_SHORT)
    {
        if (in_select)
        {
            // 濠碘槅鍋撶徊浠嬪疮椤愩倗涓?闂傚倷绶氬褍螞濞嗘挸绀夋俊銈呮噺閸嬪倹銇勯幘鍗炵仼缂佺姵婢橀…璺ㄦ崉閻氭潙浼愰梺璇叉捣閸樠団€旈崘顔嘉ч柛銉戝倸瀵查梻渚€鈧偛鑻晶顖炴煕閳轰緤鍔熸俊鍙夊姈鐎佃偐鈧稒锚閸擃噣鏌ｆ惔銏⑩姇闁挎艾顭跨捄铏剐ч柡灞糕偓鎰佸悑闁割偒鍋呯紞鍫ユ倵?menu_layer 闂傚倸鍊风粈渚€濡靛Ο鑲╃焼濞撴埃鍋撻柟顔惧亾閵堬綁宕橀妸褜妲存繝寰锋澘鈧洜鈧哎鍔戦崺鈧い鎺嗗亾闁哥喐鎸冲顐㈩吋閸涱垱娈曢梺閫炲苯澧伴柛鎺撳浮閺屻劎鈧綆鍏橀弸鏍倵楠炲灝鍔氶悗姘煎枤缁絽螖閸涱喖浠梺缁樺灦閿氶柣蹇嬪劚闇夋繝濠傚閸婃劙鏌?
            if (menu_layer == 2) {
                int idx = 0;
                for(int i = 0; i < sub_game_count; i++) {
                    if(sub_game_list[i] == selected_game) { idx = i; break; }
                }
                idx = (idx + 1) % sub_game_count;
                selected_game = sub_game_list[idx];
            } 
            else {
                int idx = 0;
                for(int i = 0; i < main_app_count; i++) {
                    if(main_app_list[i] == selected_game) { idx = i; break; }
                }
                idx = (idx + 1) % main_app_count;
                selected_game = main_app_list[idx];
            }
        }
        else
        {
            if (mode == MODE_SETTING) {
                if (setting_ui_handle_short_press()) {
                    return;
                }
            }
            else if (mode == MODE_RADIO) {
                // In radio mode, short press is reserved for station cycling instead of in-app actions.
                audio_player_next_station();
            }
            else if (mode == MODE_DINO && dino_game.state == STATE_GAMEOVER) {
                dino_game_reset(&dino_game); 
            }
            else if (mode == MODE_PLANE && air_game.state == STATE_GAMEOVER) {
                air_game_reset(&air_game);
            }
        }
    }
    else if (event == KEY_EVENT_LONG)
    {
        if (!in_select)
        {
            if (mode == MODE_SETTING && setting_ui_handle_long_press()) {
                return;
            }
            if (mode == MODE_RADIO) {
                audio_player_exit_radio_mode();
            }
            if (mode == MODE_SETTING) {
                setting_ui_reset_state();
            }
            selected_game = mode; 
            in_select = true;
            
            // 濠碘槅鍋撶徊浠嬪疮椤愩倗涓?闂傚倸鍊烽悞锕併亹閸愵亞鐭撻柣銏㈩焾閽冪喎鈹戦悩鎻掓殲濞存嚎鍊栫换娑㈠箣閻忔椿浜滈锝夊箮閼恒儮鎷哄銈嗗姂閸╁嫬危瑜版帗鍊垫慨妯稿劚婵倻鈧娲橀懝鎹愮亙闂佸憡娲嶉弬渚€宕戦幘璇茬妞ゆ棁鍋愰惈鍕煟閻樿京顦︽繝鈧潏鈺冪焼濠㈣埖鍔栭悡鏇㈡煙閸濆嫷鍎忛柍褜鍓氬ú鐔煎箖妤ｅ啫绀堢憸澶愬磻閹剧粯鍋￠柡澶嬪濞堫厾绱撴担闈涘閻忓繑鐟╅獮蹇涙偐鐠囧弬銊╂煏婢舵ê鏋熺悮鈺佲攽閻愯尙鎽犵紒顔肩灱閼洪亶鎳栭埡浣哥亰闂佸壊鍋嗛崰鎾诲煝閺囥垺鐓曟い鎰剁悼缁犳ɑ绻濋埀顒傗偓闈涙憸绾?
            if (mode == MODE_BALL || mode == MODE_DINO || mode == MODE_PLANE) {
                menu_layer = 2; // 婵犵數濮烽。浠嬪焵椤掆偓閸熷潡鍩€椤掆偓缂嶅﹪骞冨Ο璇茬窞闁归偊鍓欏宄邦渻閵堝棛澧柣鐔濆洦鍎楅柕鍫濇缁犻箖鏌熼幑鎰彧闁哥喓鍋ら弻娑㈠Ω閿旂偓鍣伴梺鍝勮嫰閹虫﹢宕洪埀顒併亜閹烘垵顏╃紒鈧崟顖涚厽闁瑰鍊栭幋锔解挃鐎广儱顦伴悡鐔兼煏婵炲灝鍔氭い蹇曟暩缁辨帗娼忛妸褏鐤勯梺鍝勮閸斿秹骞忛崨鏉戜紶闁靛闄勯惁鐐烘⒒娓氣偓濞艰崵鎷归悢鐓庣閻庯綆鍓濇慨鎶芥煃閸濆嫬鏆熸俊顐灣閹叉悂骞嬮敐鍐ф睏閻庡箍鍎遍ˇ顖滄兜閳ь剟姊洪崫鍕窛闁稿鍠庨敃銏⑩偓闈涙憸绾?
            } else {
                menu_layer = 1; // 婵犵數濮烽。浠嬪焵椤掆偓閸熷潡鍩€椤掆偓缂嶅﹪骞冨Ο璇茬窞闁归偊鍓欏宄邦渻閵堝棛澧柣鐔濆洦鍎楅柕鍫濐槹閻撴盯鏌涘鈧粈浣糕枍瀹ュ鐓冪憸婊堝礈濮橀鏁勫┑顖涘強闂傚倸鍊风欢锟犲磻閳ь剟鏌涚€ｎ偅宕岄柡灞剧洴楠炲鎮╅幓鎺戭瀱婵犵數鍋犻崑鎰板极婵犳艾绠氶柛鎰靛枛缁€瀣煕閹捐尪鍏屾い锔诲櫍濮婄粯鎷呴幖鐐扮钵缂備讲鍋撳〒姘ｅ亾闁诡喚鍋撻妶锝夊礃閵婏箑绁堕梻渚€娼ч…鍫ュ磹濡ゅ拋鏁傞弶鍫氭櫇绾惧ジ鏌ｅ▎鎰噧婵☆偅顨堢槐鐐哄川鐎涙鍘卞┑掳鍊撻悞锔剧矆閳ь剟姊哄ú璁崇凹濠㈢懓妫濋幆鈧?
            }
            return;
        }

        if (in_select)
        {
            // 濠碘槅鍋撶徊浠嬪疮椤愩倗涓?闂傚倷绶氬褍螞濞嗘挸绀夋繛鍡樻尭濮规煡姊洪鈧粔瀵哥不閺夋鐔嗛悹杞拌閻擃剚淇婇弻銉ゆ喚闁哄矉缍侀幃娆撳矗婢舵ɑ顥栭梻渚€鈧偛鑻晶顖炴煕閳轰緤鍔熸俊鍙夊姈鐎靛ジ寮堕幋婵嗘暏闂佽崵鍠愰悷銉р偓姘煎墰缁?menu_layer 婵犵數鍋為崹鍫曞箰閸洖纾块柟娈垮枤缁犳棃鏌ㄥ┑鍡╂Ц閻熸瑱绠撻弻娑㈩敃閻樿尙浼勯梻鍌氬亞閸ㄨ泛顕ｉ崼鏇炵厸闁稿本绮犻悘鍗烆渻閵堝啫鐏柛銊ょ祷瑜颁線姊绘笟鍥у缂佸娼ч悾?闂備浇宕垫慨鎾箹椤愶箑鐤柛褎顨呴崥?
            if (menu_layer == 2) {
                if (selected_game == MODE_GAME_SELECT) {
                    // 闂傚倸鍊搁崐鐢稿磻閹剧粯鐓欑紒瀣健椤庢鏌?Back 闂傚倸鍊烽悞锕傤敄濞嗘挸闂い鏍ㄧ矋椤洖霉閻撳海鎽犻柍閿嬫⒒閳ь剙绠嶉崕鍗灻洪銏″仒闁圭偓鏋煎Σ鍫ユ煙閹规劦娼愰柕鍡楀暟缁辨帡寮▎鎯ф闂佹眹鍎烘禍锝壦囬崜浣瑰枑闁哄娉曟晥閻庤娲╃换婵嗩嚕閹绢噮鏁傞柛鈩冪懐濡查攱淇婇妶鍥ラ柛瀣█瀹曟椽寮介鐔蜂壕闁割煈鍋勯崫铏光偓?Game 婵犵數濮伴崹褰掓偉閵忋倕绀冮柕濠忓閵?
                    menu_layer = 1;
                    selected_game = MODE_GAME_SELECT; 
                } else {
                    // 闂佽瀛╅鏍窗閹烘纾婚柟鍓х帛閻撴洘鎱ㄥΟ鐓庡付闁诲繒濞€閺屾稑鈻庨幇顒夊殝闂佺懓鍢查幊姗€鐛Ο鍏煎磯閻炴稈鍓濈€垫﹢姊绘担钘夊惞闁稿绋戣灋婵炲棙鎸婚崑澶愭煕椤愶絾绀€缂?                    app_mode_set(selected_game);
                    app_mode_set(selected_game);
                    in_select = false;
                }
            } 
            else { // menu_layer == 1
                if (selected_game == MODE_GAME_SELECT) {
                    // 闂傚倸鍊搁崐鐢稿磻閹剧粯鐓欑紒瀣健椤庢鏌?Game 闂傚倸鍊烽悞锕傤敄濞嗘挸闂い鏍ㄧ矋椤洖霉閸忓吋缍戠痪顓涘亾闂備焦鎮堕崕顕€寮查悩缁樺仒闁圭偓鏋煎Σ鍫ユ煙閹规劦娼愰柕鍡楀暟缁辨帡濡搁姀鈩冪彇闂佹眹鍎烘禍锝壦囬崜浣瑰枑闁哄娉曟晥閻庤娲╃换婵嗩嚕閹绢噮鏁傞柛鈩冪懐濡查攱淇婇妶鍥ラ柛瀣█瀹曟椽寮介鐔蜂壕闁割煈鍋勯崫铏光偓?Ball
                    menu_layer = 2;
                    selected_game = MODE_BALL; 
                } else {
                    // 闂佽瀛╅鏍窗閹烘纾婚柟鍓х帛閻撴洘鎱ㄥΟ鐓庡付闁诲繆鍓濈换娑㈠川椤曞棜鈧潡鏌?App (Clock, Setting, Blood)
                    app_mode_set(selected_game);
                    in_select = false;
                }
            }
            return;
        }
    }
}

// mode.c
void enter_light_sleep(void)
{
    u8g2_SetPowerSave(&u8g2, 1); // 闂傚倷娴囬鏍矗鎼淬劌鍨傚ù鐓庣摠閸?
    mpu6050_sleep(1);
    bmp280_sleep(1);
    max30102_sleep(1);
    if (sensor_task_handle != NULL) {
        vTaskSuspend(sensor_task_handle); // 闂傚倷绀侀幖顐⑽涘Δ鍛９闁告稑锕﹂々鐑芥煟濡搫鏆辨い鏇憾閺岋絽螣閸濆嫭姣愭繝纰樺墲婵炲﹪寮?I2C 闂備礁鎼ˇ閬嶅磿閹版澘绐楁繛鎴欏焺閺?
    }

    if (sync_task_handle != NULL) {
        vTaskSuspend(sync_task_handle); 
    }

    // 濠碘槅鍋撶徊浠嬪疮椤愩倗涓?闂傚倷绶氬褍螞濞嗘挸绀夐柟鐑橆殔缁犵偤鏌曟繛鍨姶婵?1闂傚倷绶氬褍螞濞嗘挸鏄ュ┑鐘插椤洖霉閸忓吋缍戠紓浣叉櫊閺岀喖顢涢崱妤佸櫧闁?ext0闂傚倷鐒︾€笛呯矙閹达箑瀚夋い鎺戝閺嬩礁鈹戦崒姘暈闁?ESP32-S3 闂備浇顕у锕傦綖婢跺孩鎳岄梻浣告惈閻ジ宕伴弽褜鍤曢柣銏㈩焾閻掑灚銇勯幒鎴濐仼缂佺姵濞婇弻鏇熺箾閸喖顬堢紓浣插亾闁告劦鍠楅悡?Light-sleep GPIO 闂傚倷绀侀幗婊堝磻閹达箑纾规繝闈涱儏閻ゎ噣鎮楅敐搴℃灈婵☆偅锕㈤弻锝夋偄缁嬫妫嗙紒缁㈠幐閸?
    gpio_wakeup_enable(USER_KEY_PIN, GPIO_INTR_LOW_LEVEL); // 婵犵數鍋犻幓顏嗗緤閽樺褰掓倻閽樺鐎梺褰掓？閼宠泛鐣垫笟鈧弻娑㈠箛椤掆偓缁狙囨煛閸℃鍤囨慨?
    esp_sleep_enable_gpio_wakeup(); 

    gpio_intr_disable(USER_KEY_PIN);
    // 濠碘槅鍋撶徊浠嬪疮椤愩倗涓?闂傚倷绶氬褍螞濞嗘挸绀夐柟鐑橆殔缁犵偤鏌曟繛鍨姶婵?2闂傚倷绶氬褍螞濞嗘挸鏄ュ┑鐘插椤洖霉閻撳海鎽犻柛搴㈡崌閺屻劌鈽夊Ο渚痪闂佸吋婢樺锟犲蓟濞戙垹绠荤€规洖娲犻弸宀€绱撴担浠嬪摵缂佽鍊块、姘舵晲閸℃劕浜板┑鐐叉閸嬫捇寮埀顒勬⒑鐠囪尙绠扮紒缁樺姇铻炴繛鍡樺灍閸嬫捇宕归銈囩厜闂佸搫琚崝鎴︺€佸▎鎾崇畾妞ゎ剦鍣粻鎾诲蓟閻旂⒈鏁勯悹鎭掑妼婵¤棄鈹戦悩娆屽亾闁稿鎸荤换婵嬪閿濆棛銆愮紓浣割槸绾绢厾妲愰悙鍝勫窛闁哄鍨归鍥⒑缁嬫寧婀扮紒璇插暙閻ｇ兘骞庨懞銉у弳濠电偞鍨堕…鍥倿閸涘﹣绻嗛柤濂割杺閸ゆ瑦銇勯鐘茬伈濠殿喒鍋撻梺缁樏悘姘跺汲閺冨牊鈷戞繛鑼额嚙娴滄劙鏌涚€ｎ偅宕岄柡灞界Ч婵＄兘鏁傞崜褏鍘戦梺璇查閻忔氨鍒掑鍥╃煓濠㈣泛澶囬崑鎾绘晬閹典礁浜鹃梺绋款儐閹告悂鍩為幋锕€閱囬柣鏃傚劋濞堟悂姊绘担绋挎倯婵炲吋鐟ч幑銏ゅ箣閿曗偓濮规煡鏌ｉ弮鍌氬付缂佺姴寮堕妵鍕籍閸屾艾浠樼紓浣哄Х閺佸寮婚敐澶樻晜閻熶降鍊曢埀顑惧€曢…鍥即閵忊€斥偓闈浢归敐鍛殭妞ゃ儱绉归弻娑㈠Φ閸楃偞鍣介柣顓熸崌閺岀喖鎮滃鍡樼暥闂?
    key_reset_fsm(KEY_USER);

    esp_light_sleep_start(); // 濠碘槅鍋撶徊浠嬪疮椤栨壕鍋?闂傚倷鐒﹀鎸庣閻愬搫绐楅柟浼村亰閺佸棙绻濇繝鍌滃闁稿骸绉归弻娑㈠即閵娿儱绠洪梺钘夊€搁澶愬箖瀹勬壋鏋庨煫鍥风稻閳诲牆鈹戦埥鍡楃仩婵☆偅鐟ч幑銏ゅ川鐎涙ê鈧兘姊婚崼鐔衡棩闁?濠碘槅鍋撶徊浠嬪疮椤栨壕鍋?

    // 濠碘槅鍋撶徊浠嬪疮椤栫偞鍋╂い鎺嶇筏缁辨棃鏌涢幇銊︽珖闁?闂傚倷绀佸﹢閬嶁€﹂崼銉嬪洭顢欓崜褏鐣堕梺鍛婄⊕濞兼瑧绮婚弶娆剧唵闁兼悂娼ф慨鍫ユ煛閸℃娲撮柡宀嬬秮椤㈡寰勬繝鍌楁嫬婵犵妲呴崑鍕焽閿熺姴鏋佺€广儱娲ｅ▽顏堟煠濞村娅囬柣鎾跺█閺屸剝寰勯崱妯荤彆闂佽崵鍟块弲鐘茬暦閹惰姤鍊婚柤鎭掑劚濞堬綁妫呴銏″缂佸鍨垮?濠碘槅鍋撶徊浠嬪疮椤栫偞鍋╂い鎺嶇筏缁辨棃鏌涢幇銊︽珖闁?

    u8g2_SetPowerSave(&u8g2, 0); // 婵犵數鍋涢悺銊у垝閻樿绐楅柡宥庡幗閸?
    mpu6050_sleep(0);
    bmp280_sleep(0);
    max30102_sleep(0);

    if (sensor_task_handle != NULL) {
        vTaskResume(sensor_task_handle); // 闂傚倷娴囬鏍储瑜版帒鍨傜憸鐗堝吹閸ヮ剙鐭楀璺侯煬閸ゃ倝姊虹紒妯忣亪宕幐搴濈箚闁规儼濮ら悡?
    }
    if (sync_task_handle != NULL) {
        vTaskResume(sync_task_handle); 
    }

    // 濠碘槅鍋撶徊浠嬪疮椤愩倗涓?闂傚倷绶氬褍螞濞嗘挸绀夐柟鐑橆殔缁犵偤鏌曟繛鍨姶婵?3闂傚倷绶氬褍螞濞嗘挸鏄ュ┑鐘插椤洖霉閻撳海鎽犻柛銈嗗姍閻擃偊宕堕妸褉妲堥梺鍝勬媼娴滎亪寮诲☉姗嗘僵妞ゆ帒瀚烽埀顒侇殘閳ь剝顫夊ú婊堝窗閺嵮屽殨妞ゆ帒鍟ㄦ禍褰掓煙閻戞ɑ灏伴柕鍡楀暣閺岋絾鎯旈敐鍡╀户缂備浇灏欓弲顐ゅ垝閳哄懏鍊婚柤鎭掑劤閸旂敻姊洪柅鐐茶嫰婢у鈧娲橀悷銉у弲濡炪倕绻嬬粈浣圭閹岀唵閻犺桨璀﹂崕蹇斻亜韫囧鍔氭い顏勫暣婵″爼宕堕妸锔界槗婵犵數鍋ゅΛ鍧楀础閹惰棄鏋佺€广儱鎳愰弳鍡涙煕閺囥劌浜濇い蹇曞枔缁辨捇宕掑顒佺亾闂佸摜濮甸〃鍫ュ焵椤掆偓閻忔氨鍒掗幘璇茶摕闁靛ň鏅╅弫濠囨煙椤栵絿浠㈤柨鏇炲€归悡娆愩亜閹达絾纭堕柛鏂跨У缁绘繆顦柛?
    in_select = false; 
    ignore_first_long_press_after_wake = true;
    setting_ui_set_wifi_reset_armed(false);
    key_reset_fsm(KEY_USER);

    // 濠碘槅鍋撶徊浠嬪疮椤愩倗涓?闂傚倷绶氬褍螞濞嗘挸绀夐柟鐑橆殔缁犵偤鏌曟繛鍨姶婵?4闂傚倷绶氬褍螞濞嗘挸鏄ュ┑鐘插椤洖霉閻撳海鎽犻柛銈嗗姍閻擃偊宕堕妸褉妲堥梺鍝勬媼娴滎亪寮婚敐澶樻晢濠㈣泛锕ら埛瀣攽閳藉棗浜滄繛灏栤偓宕囨殾婵鍩栭悡銉╂倵閿濆骸浜為柛妯诲姍濮婃椽宕崟顐ｆ闂佺粯鐗滈崢褑鐏嬮梺绉嗗嫷娈旂紒鐙呯悼缁辨帒鈽夊Ο鏄忕缂?
    last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // 闂傚倷绀侀幗婊堝磻閹达箑纾规繝闈涱儏閻ゎ噣鎮楅敐搴℃灈閻熸瑱绠撻獮鏍箹椤撶偟浠紓浣插亾濠㈣埖鍔栭悡鏇㈢叓閸ャ劍鐓ラ柍缁樻礃娣囧﹪顢曢姀鈺佹闂佸綊顥撴繛鈧┑锛勫厴婵＄柉顦撮柍瑙勭洴濮婃椽鎮℃惔锝嗘喖濠殿喗菧閸斿矂鍩㈤幘璇茬闁挎洍鍋撻柤绋跨秺閺屾洟宕煎┑鎰﹂梺鍝勬媼娴滎亪寮诲☉銏犵睄闁稿本纰嶉悘鍡涙⒑缁嬭法绠查柨鏇樺灲瀵偄顓奸崨顖涙畷闂佸憡娲﹂崕鐑藉煛閸屾稑寮垮┑鐘绘涧鐎氼剟鎮橀埡鍛拺閻熸瑱绲惧▍濠勨偓娈垮櫘閸ｏ綁鐛崱娑樼妞ゆ梻铏庨崥瀣⒑閼姐倕浠︾紒瀣灴瀵敻顢楅埀顒勨€旈崘顔肩＜闁绘劙娼х粊锕傛⒑閸涘﹦鎳冩い锔诲灦钘熺€广儱妫欓崣蹇旂節閸偅灏甸柛鎺嶅嵆閺岋綁寮介鈧悡鎰磼鐎ｎ亶妯€妞ゃ垺锕㈤幃銏ゅ传閵夈儱楔
    gpio_wakeup_disable(USER_KEY_PIN); 

    while (gpio_get_level(USER_KEY_PIN) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    gpio_intr_enable(USER_KEY_PIN);
}
