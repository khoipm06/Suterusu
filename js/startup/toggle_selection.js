(function() {
    const id = 'selection-style';
    
    // Function to toggle selection
    function toggleSelection() {
        const existing = document.getElementById(id);
        if (existing) {
            existing.remove();
            console.log('[Suterusu] Text selection enabled');
        } else {
            const style = document.createElement('style');
            style.id = id;
            style.textContent = `
                ::selection {
                background: transparent !important;
                color: inherit !important;
            }`;
            (document.head || document.documentElement).appendChild(style);
            console.log('[Suterusu] Text selection hidden');
        }
    }
    
    // Listen for F9 key which is safe because no browser uses it by default
    document.addEventListener('keydown', function(e) {
        if (e.key === 'F9') {
            e.preventDefault();
            toggleSelection();
        }
    });
    
    console.log('[Suterusu] Toggle selection script loaded (Press F9)');
})();
