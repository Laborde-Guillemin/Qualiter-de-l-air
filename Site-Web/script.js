window.onload = function() {
    fetchData();
    setInterval(fetchData, 5000); // Met à jour les données toutes les 5 secondes
};

function fetchData() {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
        if (xhr.readyState === XMLHttpRequest.DONE) {
            if (xhr.status === 200) {
                var data = JSON.parse(xhr.responseText);
                updateSensorData(data);
                updateImage(data);
            } else {
                console.error('Erreur lors de la récupération des données du capteur');
            }
        }
    };
    xhr.open('GET', '/data', true);
    xhr.send();
}

// Fonction de mise à jour des valeurs du site web
function updateSensorData(data) {
    document.getElementById('temperature').textContent = data.temperature + "°C";
    document.getElementById('humidity').textContent = data.humidity + "%";
    document.getElementById('COV').textContent = data.COV + "/25000";
    document.getElementById('Forme_Alde').textContent = data.FormeAlde + "µg/m³";
    document.getElementById('CO2').textContent = data.CO2 + "ppm";
    document.getElementById('PM_1').textContent = data.PM_1 + "µg/m³";
    document.getElementById('PM_25').textContent = data.PM_25 + "µg/m³";
    document.getElementById('PM_10').textContent = data.PM_10 + "µg/m³";
}

function updateImage(data) {
    var imageContainer = document.getElementById('imageContainer');
    imageContainer.innerHTML = '';

    // Condition pour afficher l'image en fonction d'une certaine valeur
    if (data.COV > 300) {
        var img = document.createElement('img');
        img.src = 'Img/COV.png'; // Remplacez par le chemin de votre image
        img.alt = 'Alerte de haute concentration de COV';
        imageContainer.appendChild(img);
    } else if (data.CO2 > 800) {
        var img = document.createElement('img');
        img.src = 'Img/CO2.png'; // Remplacez par le chemin de votre image
        img.alt = 'Alerte de haute concentration de CO2';
        imageContainer.appendChild(img);
    } else if (data.FormeAlde > 45) {
        var img = document.createElement('img');
        img.src = 'Img/FormeAld.png'; // Remplacez par le chemin de votre image
        img.alt = 'Alerte de haute concentration de Formaldéhyde';
        imageContainer.appendChild(img);
    }
    // Ajoutez d'autres conditions si nécessaire
}