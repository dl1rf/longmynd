'use strict';

const ws_url = "ws://" + window.location.hostname + ":" + window.location.port + "/";

let ws_monitor_buffer = [];
let ws_control_buffer = [];

let ws_monitor = new strWebsocket(ws_url, "monitor", ws_monitor_buffer);
let ws_control = new strWebsocket(ws_url, "control", ws_control_buffer);

let render_timer = null;
let render_busy = false;
let render_interval = 100;

let rx_status = null;
let ts_status = null;

const demod_state_lookup = {
  0: "Initialising",
  1: "Hunting",
  2: "Header..",
  3: "Lock DVB-S",
  4: "Lock DVB-S2"
};

const modcod_lookup_dvbs = {
  4: "QPSK 1/2",
  5: "QPSK 3/5",
  6: "QPSK 2/3",
  7: "QPSK 3/4",
  9: "QPSK 5/6",
  10: "QPSK 6/7",
  11: "QPSK 7/8"
}

const modcod_lookup_dvbs2 = {
  0: "DummyPL",
  1: "QPSK 1/4",
  2: "QPSK 1/3",
  3: "QPSK 2/5",
  4: "QPSK 1/2",
  5: "QPSK 3/5",
  6: "QPSK 2/3",
  7: "QPSK 3/4",
  8: "QPSK 4/5",
  9: "QPSK 5/6",
  10: "QPSK 8/9",
  11: "QPSK 9/10",
  12: "8PSK 3/5",
  13: "8PSK 2/3",
  14: "8PSK 3/4",
  15: "8PSK 5/6",
  16: "8PSK 8/9",
  17: "8PSK 9/10",
  18: "16APSK 2/3",
  19: "16APSK 3/4",
  20: "16APSK 4/5",
  21: "16APSK 5/6",
  22: "16APSK 8/9",
  23: "16APSK 9/10",
  24: "32APSK 3/4",
  25: "32APSK 4/5",
  26: "32APSK 5/6",
  27: "32APSK 8/9",
  28: "32APSK 9/10"
}

const mpeg_type_lookup = {
  1: "MPEG1 Video",
  3: "MPEG1 Audio",
  15: "AAC Audio",
  16: "H.263 Video",
  27: "H.264 Video",
  33: "JPEG2K Video",
  36: "H.265 Video",
  129: "AC3 Audio"
}

const rflevel_lookupfn = function(agc1_value, agc2_value)
{
  const agc1_lookup = [
    [-39, 10],
    [-38, 21800],
    [-37, 25100],
    [-36, 27100],
    [-35, 28100],
    [-34, 28900],
    [-33, 29600],
    [-32, 30100],
    [-31, 30550],
    [-30, 31000],
    [-29, 31350],
    [-28, 31700],
    [-27, 32050],
    [-26, 32400],
    [-25, 32700],
    [-24, 33000],
    [-23, 33301],
    [-22, 33600],
    [-21, 33900],
    [-20, 34200],
    [-19, 34500],
    [-18, 34750],
    [-17, 35000],
    [-16, 35250],
    [-15, 35500],
    [-14, 35750],
    [-13, 36000],
    [-12, 36200],
    [-11, 36400],
    [-10, 36600],
    [-9, 36800],
    [-8, 37000],
    [-7, 37200],
    [-6, 37400],
    [-5, 37600],
    [-4, 37700]
  ];

  const agc2_lookup = [
    [-67, 3200],
    [-66, 2740],
    [-65, 2560],
    [-64, 2380],
    [-63, 2200],
    [-62, 2020],
    [-61, 1840],
    [-60, 1660],
    [-59, 1480],
    [-58, 1300],
    [-57, 1140],
    [-56, 1000],
    [-55, 880],
    [-54, 780],
    [-53, 700],
    [-52, 625],
    [-51, 560],
    [-50, 500],
    [-49, 450],
    [-48, 400],
    [-47, 360],
    [-46, 325],
    [-45, 290],
    [-44, 255],
    [-43, 225],
    [-42, 200],
    [-41, 182],
    [-40, 164],
    [-39, 149],
    [-38, 148]
  ];

  if(agc1_value > 1000)
  {
    let agc1_lookup_closest = agc1_lookup.reduce(function(prev, curr) {
      return (Math.abs(curr[1] - agc1_value) < Math.abs(prev[1] - agc1_value) ? curr : prev);
    });
    return agc1_lookup_closest[0];
  }
  else if(agc2_value > 150)
  {
    let agc2_lookup_closest = agc2_lookup.reduce(function(prev, curr) {
      return (Math.abs(curr[1] - agc2_value) < Math.abs(prev[1] - agc2_value) ? curr : prev);
    });
    return agc2_lookup_closest[0];
  }
  else
  {
    return -38;
  }
}

$(document).ready(function()
{
  /* Set up configure */
  $("#submit-freq-sr").click(function(e)
  {
    e.preventDefault();

    let input_frequency_value = parseInt($("#input-frequency").val());

    if(isNaN(input_frequency_value))
    {
      input_frequency_value = parseInt($("#input-qo100frequency").val()) - 9750000;
    }
    let input_symbolrate_value = parseInt($("#input-symbolrate").val());

    if(input_frequency_value != 0 && input_symbolrate_value != 0)
    {
      ws_control.sendMessage("C"+input_frequency_value+","+input_symbolrate_value);
    }
  });
  $("#beacon-freq-sr").click(function(e)
  {
    e.preventDefault();
    $("#input-qo100frequency").val("10492500");
    $("#input-symbolrate").val("2000");
  });
  /*
  {"type":"status","timestamp":1571256202.388,"packet":{"rx":{"demod_state":4,"frequency":742530,"symbolrate":1998138,
  "vber":0,"ber":1250,"mer":80,"modcod":6,"short_frame":false,"pilot_symbols":true,
  "constellation":[[221,227],[19,213],[35,44],[203,213],[51,62],[77,221],[229,219],[234,35],[199,57],[31,230],[216,210],[228,38],[24,221],[247,31],[230,207],[237,203]]},
  "ts":{"service_name":"A71A","service_provider_name":"QARS","null_ratio":0,"PIDs":[[257,27],[258,3]]}}}
*/
  /* Render to fields */
  function render_status(data_json)
  {
    let status_obj;
    let status_packet;
    try {
      status_obj = JSON.parse(data_json);
      if(status_obj != null)
      {
        //console.log(status_obj);
        rx_status = status_obj.packet.rx;

        let rflevel_dbm = rflevel_lookupfn(rx_status.agc1, rx_status.agc2);
        $("#valuedisplay-rflevel").text(rflevel_dbm+"dBm");
        $("#progressbar-rflevel").css('width', ((rflevel_dbm+40)*(100.0/35)+'%')).attr('aria-valuenow', rflevel_dbm);

        $("#badge-state").text(demod_state_lookup[rx_status.demod_state]);
        $("#span-status-frequency").text(rx_status.frequency+"KHz");
        $("#span-status-symbolrate").text(rx_status.symbolrate+"KS");
        if(rx_status.demod_state == 3) // DVB-S
        {
          $("#span-status-modcod").text(modcod_lookup_dvbs[rx_status.modcod]);
        }
        else if(rx_status.demod_state == 4) // DVB-S2
        {
          $("#span-status-modcod").text(modcod_lookup_dvbs2[rx_status.modcod]);
        }
        else
        {
          $("#span-status-modcod").text("");
        }
        $("#progressbar-mer").css('width', (rx_status.mer/3.1)+'%').attr('aria-valuenow', rx_status.mer).text(rx_status.mer/10.0+"dB");

        $("#progressbar-vber").css('width', (rx_status.vber)+'%').attr('aria-valuenow', rx_status.vber).text(rx_status.vber/10.0+"%");

        $("#progressbar-ber").css('width', (rx_status.ber)+'%').attr('aria-valuenow', rx_status.ber).text(rx_status.ber/10.0+"%");
        
        constellation_draw(rx_status.constellation);

        ts_status = status_obj.packet.ts;

        $("#progressbar-ts-null").css('width', (ts_status.null_ratio)+'%').attr('aria-valuenow', ts_status.null_ratio).text(ts_status.null_ratio+"%");

        $("#span-status-name").text(ts_status.service_name);
        $("#span-status-provider").text(ts_status.service_provider_name);

        let ulTsPids = $('<ul />');
        for (pid in ts_status.PIDs) {
          $('<li />')
            .text(ts_status.PIDs[pid][0]+": "+mpeg_type_lookup[ts_status.PIDs[pid][1]])
            .appendTo(ulTsPids);
        }
        $("#div-ts-pids").empty();
        $("#div-ts-pids").append(ulTsPids);

      }
    }
    catch(e)
    {
      console.log("Error parsing message!",e);
    }
  }


  /* Set up listener for websocket */
  render_timer = setInterval(function()
  {
    if(!render_busy)
    {
      render_busy = true;
      if(ws_monitor_buffer.length > 0)
      {
        /* Pull newest data off the buffer and render it */
        let data_frame = ws_monitor_buffer.pop();

        render_status(data_frame);

        ws_monitor_buffer.length = 0;
      }
      render_busy = false;
    }
    else
    {
      console.log("Slow render blocking next frame, configured interval is ", render_interval);
    }
  }, render_interval);
});
