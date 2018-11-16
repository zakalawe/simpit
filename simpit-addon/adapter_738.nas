

print("Loading 737-800 hardware adaption layer");

var mcpCallbackTable = {
    'speed': autopilot737.speed_button_press,
    'vnav': autopilot737.vnav_button_press,
    'altitude-hold': autopilot737.althld_button_press,
    'approach': autopilot737.app_button_press,
    'lnav': autopilot737.lnav_button_press,
    'vor' : autopilot737.vorloc_button_press,
    'command-a' : autopilot737.cmda_button_press,
    'command-b' : autopilot737.cmdb_button_press,
    'cws-a' : autopilot737.cwsa_button_press,
    'cws-b' : autopilot737.cwsb_button_press,
    'vs' : autopilot737.vs_button_press,
    'level-change' : autopilot737.lvlchg_button_press,
    'speed-crossover' : autopilot737.changeover_button_press,
    'n1' : autopilot737.n1_button_press,
    'heading' : autopilot737.hdg_button_press
};

var mcpCommandCallback = func(node)
{
    var cmd = node.getChild("button").getValue();
    if (!contains(mcpCallbackTable, cmd)) {
        printlog("warn", "GoFlight MCP: no callback registered for " ~ cmd);
        return;
    }

    var cb = mcpCallbackTable[cmd];
    cb();
};

addcommand('goflight-mcp-button', mcpCommandCallback);

var apDisengageProp = props.globals.getNode("/input/goflight/mcp/ap-disengage", 1);
setlistener(apDisengageProp, func() { 
    if (apDisengageProp.getValue()) {
        print("Doing AP disengage");
        autopilot737.apdsng_button_press();
    }
});

# AT arm prop : var AT_arm = getprop("/autopilot/internal/SPD");

var atArmProp = props.globals.getNode("/input/goflight/mcp/autothrottle-armed", 1);
setlistener(atArmProp, func() { 
    # manual sync to the 737 AP property
    setprop("/autopilot/internal/SPD", atArmProp.getValue());
});

# Captain and F/O flight director switches
var makeAlias = func(src, target) {
    var sNode = props.globals.getNode(src, 1);
    var tNode = props.globals.getNode(target, 1);
    tNode.alias(sNode);
};

makeAlias("/input/goflight/mcp/captain-fd-enabled",
    "/instrumentation/flightdirector/fd-left-on");
makeAlias("/input/goflight/mcp/fo-fd-enabled",
    "/instrumentation/flightdirector/fd-right-on");

# MCP LED outputs

var watchAPInternalProp = func(propName, ledName)
{
    goflight.mcp.watchPropertyForLED('/autopilot/internal/' ~ propName, ledName);
}

watchAPInternalProp('LNAV-NAV-light', 'VOR-LOC');
watchAPInternalProp('LNAV', 'LNAV');
watchAPInternalProp('LNAV-HDG', 'HDG-SEL');
watchAPInternalProp('CMDA', 'CMD A');
watchAPInternalProp('CMDB', 'CMD B');
watchAPInternalProp('LVLCHG', 'LVL-CHG');
watchAPInternalProp('SPD-N1', 'N1');
watchAPInternalProp('SPD-SPEED', 'SPEED');
watchAPInternalProp('VNAV', 'VNAV');
watchAPInternalProp('VNAV-GS-armed', 'APP');
watchAPInternalProp('VNAV-VS', 'V/S');
watchAPInternalProp('VNAV-ALT-light', 'ALT-HLD');

# 'SPD' seems weird but correspond to the throttled being armed, as opposed to
# only working when active, which causes incorrect behaviour when descending
# under LVL-CHG (and probably also VNAV)
watchAPInternalProp('SPD', 'A/T ARM');

goflight.mcp.watchPropertyForLED("/input/goflight/mcp/captain-fd-enabled", 'CAP F/D');
goflight.mcp.watchPropertyForLED("/input/goflight/mcp/fo-fd-enabled", 'F/O F/D');

goflight.mcp.setAltitudeFtProp("/autopilot/settings/target-altitude-mcp-ft");

# VS window blanking
var vsWindowProp = props.globals.getNode("/autopilot/internal/VNAV-VS", 1);
setlistener(vsWindowProp, func() { 
    # invert sense and set
    setprop("/input/goflight/mcp/blank-vs-window", !vsWindowProp.getValue());
}, 1);

# speed (IAS / Mach) crossover

var speedMachProp = props.globals.getNode("/autopilot/internal/SPD-MACH", 1);
setlistener(speedMachProp, func() { 
    var v = speedMachProp.getValue();
    goflight.mcp.setMachMode(v);
}, 1);

## EFIS ############################################

# <script>boeing737.efis_ctrl(0,"RANGE",1);</script>

var efisRange = props.globals.getNode("/instrumentation/efis/inputs/range-knob", 1);
setlistener(efisRange, func() { 
    # run the RANGE update with 0 change, since we already
    # wrote the new value to efis/inputs/range-knob
    boeing737.efis_ctrl(0,"RANGE",0);
});

var ndMode = props.globals.getNode("/instrumentation/efis/mfd/mode-num", 1);
setlistener(ndMode, func() { 
    # run the MODE update with 0 change, since we already
    # wrote the new value to efis/mfd/mode-num
    boeing737.efis_ctrl(0,"MODE",0);
});

var efisBaro = props.globals.getNode("/instrumentation/efis/inputs/baro-knob", 1);
var previousBaroKnob = efisBaro.getValue();

setlistener(efisBaro, func() { 
    var v = efisBaro.getValue();
    var action = v - previousBaroKnob;
    previousBaroKnob = v;
    boeing737.efis_ctrl(0,"BARO", action); 
});

print("\nDone loading 737-800 hardware adaption layer\n");

