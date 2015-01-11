 $(document).ready(function() {
 	$('#target').tooltipster({
                content:"Loading...",
		contentAsHTML:"true",
		theme:"tooltipster-duc",
		position:"top-left"
            });
	

	});


function debounce(fn, delay) {
  var timer = null;
  return function () {
    var context = this, args = arguments;
    clearTimeout(timer);
    timer = setTimeout(function () {
      fn.apply(context, args);
    }, delay);
  };
}


function GetQueryStringParams(sParam)
{
    var sPageURL = window.location.search.substring(1);
    var sURLVariables = sPageURL.split('&');
    for (var i = 0; i < sURLVariables.length; i++)
    {
        var sParameterName = sURLVariables[i].split('=');
        if (sParameterName[0] == sParam)
        {
            return sParameterName[1];
        }
    }
}




$( "#target" ).mousemove(debounce (function( event ) {

    $('#target').tooltipster('hide');    

    var msg = "Handler for .mousemove() called at ";
    msg += event.pageX + ", " + event.pageY;
	
    //offset the X&Y for the image
    var parentOffset = $(this).offset();
    var relX = Math.trunc(event.pageX - parentOffset.left);
    var relY = Math.trunc(event.pageY - parentOffset.top);
    var path = GetQueryStringParams("path");

    url = "/cgi-bin/duc.cgi";
    //console.log ("json url = " + url);
    passdata = "cmd=JSON&path=" + path + "&?" + relX + "," + relY;
    //console.log ("json passdata = " + passdata);

    //$.getJSON(url,passdata);
   path="";
   size = ""; 
   count = 0;
	
    $.getJSON(url, passdata).done(function( json ) {
      
      if (json.path) {
      	path = json.path;
      	size = json.size;
      	count = json.count;
      	//console.log( "JSON path: " + json.path );
      	//console.log( "JSON size: " + json.size );
      	//console.log( "JSON count: " + json.count );
	
      	myContent = path + "<hr>" +
		  size + " (incl. subfolders)<br> " +count + " items<BR>"; 
	
      	//console.log( myContent );

    	
    	$('#target').tooltipster('content', myContent);    
    	$('#target').tooltipster('option', "offsetX", relX);    
    	$('#target').tooltipster('option', "offsetY", -relY);    
    	$('#target').tooltipster('reposition');    

    	$('#target').tooltipster('show');    
      }
    });

 }, 100));





