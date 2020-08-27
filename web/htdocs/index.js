const ws_url = "ws://" + window.location.hostname + ":" + window.location.port + "/";

var ws_monitor_buffer = [];
var ws_control_buffer = [];

var ws_monitor = new strWebsocket(ws_url, "monitor", ws_monitor_buffer);
var ws_control = new strWebsocket(ws_url, "control", ws_control_buffer);

var render_busy = false;
var render_interval = 100;

var demod_state_lookup = {
  0: "Initialising",
  1: "Hunting",
  2: "Header..",
  3: "Lock DVB-S",
  4: "Lock DVB-S2"
};

var modcod_lookup_dvbs = {
  4: "QPSK 1/2",
  5: "QPSK 3/5",
  6: "QPSK 2/3",
  7: "QPSK 3/4",
  9: "QPSK 5/6",
  10: "QPSK 6/7",
  11: "QPSK 7/8"
}

var modcod_lookup_dvbs2 = {
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

var mpeg_type_lookup = {
  1: "MPEG1 Video",
  3: "MPEG1 Audio",
  15: "AAC Audio",
  16: "H.263 Video",
  27: "H.264 Video",
  33: "JPEG2K Video",
  36: "H.265 Video",
  129: "AC3 Audio"
}

$(document).ready(function()
{
  /* Set up configure */
  $("#submit-freq-sr").click(function(e)
  {
    e.preventDefault();

    var input_frequency_value = parseInt($("#input-frequency").val());

    if(isNaN(input_frequency_value))
    {
      input_frequency_value = parseInt($("#input-qo100frequency").val()) - 9750000;
    }
    var input_symbolrate_value = parseInt($("#input-symbolrate").val());

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
    var status_obj;
    var status_packet;
    try {
      status_obj = JSON.parse(data_json);
      if(status_obj != null)
      {
        console.log(status_obj);
        rx_status = status_obj.packet.rx;

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

        var ulTsPids = $('<ul />');
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
        var data_frame = ws_monitor_buffer.pop();

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