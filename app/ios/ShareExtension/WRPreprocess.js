// Safari share preprocessing for the Tabernacle Tools extension: returns what is already on screen (the selection, or the
// page's visible text) - no network requests. The app's importer crops the chord chart out of the page text.
var WRPreprocess = function() {};
WRPreprocess.prototype = {
    run: function (args) {
        var sel = window.getSelection ? String (window.getSelection()) : "";
        var pre = document.querySelectorAll ("pre"), pageText = "";
        for (var i = 0; i < pre.length; i++) pageText += pre[i].innerText + "\n";   // chord sites usually keep the chart in <pre>
        if (pageText.trim().length === 0 && document.body) pageText = document.body.innerText;
        args.completionFunction ({ "selection": sel, "pageText": pageText, "title": document.title });
    }
};
var ExtensionPreprocessingJS = new WRPreprocess;
