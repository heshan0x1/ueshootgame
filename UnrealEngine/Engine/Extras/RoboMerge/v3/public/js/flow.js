// Copyright Epic Games, Inc. All Rights Reserved.

class FlowOptions{
    showOnlyForced = false;
    showFiltered = false;
    showGroups = false;
    hideDisconnected = false;
}


function setDefault(map, key, def) {
    const val = map.get(key);
    if (val) {
        return val;
    }
    map.set(key, def);
    return def;
}

function renderGraph(src) {
    return Viz.instance().then(function (viz) {
        try {
            return viz.renderSVGElement(src);
        }
        catch (err) {
            console.log(src)
            throw err
        }
    })
}

function parseOptions(search) {

    let options = new FlowOptions();
    
    options.showOnlyForced = !!search.match(/showOnlyForced/i);
    options.showFiltered = !!search.match(/showFiltered/i);
    options.showGroups = !!search.match(/showGroups/i);
    options.hideDisconnected = !!search.match(/hideDisconnected/i);

    return options;

}

function makeGraph(data, args) {
    const options = args;
    const singleBotName = args.singleBotName;
    if (singleBotName) {
        return (new Graph(data, options)).singleBot(singleBotName);
    }
    const botNames = args.botNames;
    if (botNames.length > 0) {
        options.botsToShow = botNames.map(s => s.toUpperCase());
    }
    return (new Graph(data, options)).allBots();
}

function getAllBots(data) {
    let allBots = new Set();

    for (branch of data) {
        allBots.add(branch.bot);
    }

    return allBots;
}

function savePNG() {
    const svg = document.getElementById("svgSpan").querySelector('svg');
    const svgBoundingRect = svg.getBoundingClientRect();

    var canvas = document.getElementById("cnvs");
    canvas.width = svgBoundingRect.width*1.02; // 2% wider seems to be needed to actually fit the image on the canvas
    canvas.height = svgBoundingRect.height;

    const svgData = new XMLSerializer().serializeToString(svg);
    const imgBlob = new Blob([svgData], { type: 'image/svg+xml' });
    var img = new Image();
    img.onload = function () {
        canvas.getContext('2d').drawImage(img, 0, 0);
        var a = document.createElement('a');
        a.href = canvas.toDataURL('image/png').replace('image/png', 'octet/stream');
        a.download = 'botGraph.png'; // TODO: Dynamic Name Generation
        a.click();
    };
    const win = window.URL || window.webkitURL || window;
    img.src = win.createObjectURL(imgBlob);
}

function showFlowGraph(data, args, addlegend) {
    const lines = makeGraph(data, args);
    const graphContainer = $('<div class="clearfix">');
    const flowGraph = $('<div class="flow-graph" style="display:inline-block;">').appendTo(graphContainer);
    flowGraph.append($('<div>').css('text-align', 'center').text("Building graph..."));
    renderGraph(lines.join('\n'))
        .then(svg => {

        if(addlegend)
        {
            $('#graph-key-template')
            .clone()
            .removeAttr('id')
            .css('display', 'inline-block')
            .appendTo(graphContainer);
        }
        

        $('#graph-loading-text').hide();
        const span = $('<span id="svgSpan">').css('margin', 'auto').html(svg);
        const svgEl = $('svg', span).addClass('branch-graph').removeAttr('width');
        // scale graph to 70% of default size
        const height = Math.round(parseInt(svgEl.attr('height')) * .7);
        svgEl.attr('height', height + 'px').css('vertical-align', 'top');
        flowGraph.empty();
        flowGraph.append($('<img src="/img/save.png" style="position:absolute" onclick="savePNG()" title="Save Graph as PNG" width="16">'));
        flowGraph.append(span);
        flowGraph.append($('<canvas id="cnvs" class="hidden"></canvas>'))
    });
    return graphContainer;
}
